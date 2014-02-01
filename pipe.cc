// pipe.cc

#include "pipe.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <zlib.h>
#include <string>
#include <vector>
#include <algorithm>

#define ILLEGAL(n)  (n < 0 && errno != EINTR && errno != EAGAIN)
#define RESETFD(fd) do { close(fd); fd = -1; } while(0)

using std::min;
using std::vector;

namespace {

static bool stop_flag;

inline useconds_t GetNowTime() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
}

inline void NonBlocking(int fd, int on) {
  if (ioctl(fd, FIONBIO, &on) < 0)
    warn("%s: ioctl(FIONBIO, %d) error", __func__, on);
}

bool ZipCompress(int level, vector<char> *buffer, size_t *n) {
  static vector<char> zbuffer;
  size_t zn = buffer->capacity();
  zbuffer.reserve(zn);

  int res = compress2((unsigned char *)(zbuffer.data()), &zn,
                      (const unsigned char *)(buffer->data()), *n, level);
  if (res == Z_OK) {
    buffer->swap(zbuffer);
    *n = zn;
  } else if (res == Z_MEM_ERROR) {
    warnx("%s: Z_MEM_ERROR: out of memory", __func__);
  } else if (res == Z_BUF_ERROR) {
    warnx("%s: Z_BUF_ERROR: out of room in the output buffer", __func__);
  } else if (res == Z_STREAM_ERROR) {
    warnx("%s: Z_STREAM_ERROR: invalid compress level", __func__);
  } else {
    warnx("%s: unknown error", __func__);
  }

  return res == Z_OK;
}

int TcpNonBlockConnect(const char *host, const char *serv) {
  int s, rv;
  struct addrinfo hints, *servinfo, *p;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(host, serv, &hints, &servinfo)) != 0) {
    warnx("%s: getaddrinfo() error: %s", __func__, gai_strerror(rv));
    return -1;
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
      continue;

    NonBlocking(s, 1);
    if (connect(s, p->ai_addr, p->ai_addrlen) < 0 && errno != EINPROGRESS) {
      close(s);
      continue;
    }

    /* break with the socket */
    break;
  }
  if (p == NULL) {
    warn("%s: error for %s, %s", __func__, host, serv);
    return -1;
  }

  freeaddrinfo(servinfo);
  return s;
}

}  // anonymous namespace

namespace v {

HttpPipe::HttpPipe()
    : buffer_size_(2097152),  // 2M
      connect_retry_(3),
      idle_transfer_(3),
      stop_flag_(&stop_flag),
      transfer_rate_(100000),  // 100K
      zip_level_(0),  // disable
      header_(NULL),
      in_offset_(0),
      in_length_(0),
      out_offset_(0),
      out_length_(0),
      content_length_(0),
      infd_(STDIN_FILENO),
      host_(),
      port_(),
      path_(),
      connected_(false),
      request_state_(HTTP_HEAD),
      response_state_(HTTP_HEAD),
      http_flow_(HTTP_REQUEST) {
  // empty      
}

void HttpPipe::Init(int infd, const char *outurl) {
  if (infd >= 0)
    infd_ = infd;
  if (outurl)
    ParseURL(outurl);

  inbuf_.reserve(buffer_size_);
  outbuf_.reserve(buffer_size_);
}

void HttpPipe::Serve(int timeout) {
  struct pollfd fds[2] = {
    {infd_, POLLIN}, {-1, POLLIN},
  };

  int idle = 0;
  int retry = 0;
  int delay = 0;
  int interval = timeout;

  while (!*stop_flag_) {
    if (retry >= connect_retry_)
      break;

    int status = CheckTransfer(&idle);
    if (status == -1 && fds[0].fd == -1)
      break;

    SetOutput(status == 1, &fds[1]);

    time_t before = time(NULL);
    int res = poll(fds, 2, interval * 1000);

    if (res == 0) {
      idle = 0;
    } else if (res < 0 && errno != EINTR) {
      err(1, "%s: poll() error", __func__);
    } else if (res > 0) {
      HandleError(&fds[1], &retry);
      HandleOutput(&fds[1], &retry);
      HandleInput(&fds[0]);
    }

    delay += time(NULL) - before;
    if (delay < timeout) {
      interval = timeout - delay;
    } else {
      interval = timeout;
      delay = 0;
    }
  }
}

int HttpPipe::SetBufferSize(int n) {
  int old = buffer_size_;
  if (n >= 0) {
    inbuf_.reserve(n);
    outbuf_.reserve(n);
    buffer_size_ = n;
  }
  return old;
}

int HttpPipe::SetConnectRetry(int n) {
  int old = connect_retry_;
  if (n >= 0)
    connect_retry_ = n;
  return old;
}

int HttpPipe::SetIdleTransfer(int n) {
  int old = idle_transfer_;
  if (n >= 0)
    idle_transfer_ = n;
  return old;
}

bool * HttpPipe::SetStopFlag(bool *p) {
  bool *old = stop_flag_;
  if (p)
    stop_flag_ = p;
  return old;
}

int HttpPipe::SetTransferRate(int n) {
  int old = transfer_rate_;
  if (n >= 0)
    transfer_rate_ = n;
  return old;
}

int HttpPipe::SetZipLevel(int n) {
  int old = zip_level_;
  if (n >= 0)
    zip_level_ = n;
  return old;
}

Header * HttpPipe::SetHeader(Header *p) {
  Header *old = header_;
  if (p) {
    p->SetRequest("POST", path_, "HTTP/1.1");
    header_ = p;
  }
  return old;
}

ssize_t HttpPipe::ReadInput(int fd) {
  if (in_offset_ == inbuf_.capacity()) {
    warnx("pipe input OVERFLOW, overwriting.");
    in_offset_ = 0;  // overwrite
  }

  ssize_t n = read(fd, &inbuf_[in_offset_], inbuf_.capacity() - in_offset_);
  if (n > 0)
    in_offset_ += n;
  return n;
}

ssize_t HttpPipe::SendRequest(int fd, bool *finished) {
  ssize_t res = 0;
  size_t n = transfer_rate_ ?
      min<long>(transfer_rate_, in_length_) : in_length_;

  if (content_length_ == 0) {
    if (zip_level_ > 0) {
      ssize_t save = n;
      header_->SetField("LETV-ZIP",
                        ZipCompress(zip_level_, &outbuf_, &n) ? "1" : NULL);
      in_length_ -= save - n;
    }
    content_length_ = out_length_ = n;
  } else if (n > out_length_){
    n = out_length_;
  }

  switch (request_state_) {
    case HTTP_HEAD:
      res = SendHead(fd, n);
      break;
    case HTTP_BODY:
      res = SendBody(fd, n);
      break;
  }

  if (out_length_ == 0) {
    request_state_ = HTTP_HEAD;
    in_length_ -= content_length_;
    content_length_ = 0;
    out_offset_ = 0;
    *finished = true;
  } else {
    request_state_ = HTTP_BODY;
    *finished = false;
  }
  return res;
}

ssize_t HttpPipe::SendHead(int fd, size_t n) {
  assert(n <= out_length_);
  long head_size = 0;
  const char *header = header_->Generate(content_length_,
                                         &head_size) + out_offset_;
  head_size -= out_offset_;

  struct iovec iov[2];  // [0]: head, [1]: body
  iov[0].iov_base = (void *)header;
  iov[0].iov_len = head_size;
  iov[1].iov_base = &outbuf_[0];
  iov[1].iov_len = n;

  ssize_t res = writev(fd, iov, 2);
  size_t ndata = 0;
  if (res > 0) {
    if (res < head_size) {
      out_offset_ += res;
    } else {
      ndata = res - head_size;
      out_offset_ = ndata;
      out_length_ -= ndata;
    }
  }
  return res;
}

ssize_t HttpPipe::SendBody(int fd, size_t n) {
  assert(n <= out_length_);
  ssize_t res = write(fd, &outbuf_[out_offset_], n);
  if (res > 0) {
    out_offset_ += res;
    out_length_ -= res;
  }
  return res;
}

ssize_t HttpPipe::GetResponse(int fd, bool *finished) {
  enum { MAX_QUERY = 2048 };
  outbuf_.reserve(MAX_QUERY);

  ssize_t res;
  if (response_state_ == HTTP_HEAD) {
    res = GetHead(fd);
    if (res <= 0)
      goto finish;

    const char *p = NULL;
    if ((p = strcasestr(outbuf_.data(), "Content-Length:")) != NULL)
      content_length_ = strtoul(p + 15, NULL, 10);
  }

  assert(response_state_ == HTTP_BODY);
  res = GetBody(fd);

finish:
  if (res == 0 || ILLEGAL(res) ||
      (response_state_ == HTTP_BODY && content_length_ == 0)) {
    response_state_ = HTTP_HEAD;
    out_offset_ = content_length_ = 0;
    *finished = true;
  } else {
    *finished = false;
  }
  return res;
}

ssize_t HttpPipe::GetHead(int fd) {
  int i = 0;
  char s[2] = "";
  ssize_t res = 0;

  while (true) {
    res = read(fd, s, 1);
    if (res <= 0)
      break;

    if (*s == '\r') {
      continue;
    } else if (*s == '\n') {
      if(i == 0) {
        response_state_ = HTTP_BODY;
        break;
      }
      i = 0;
    } else {
      ++i;
    }

    outbuf_[out_offset_++] = *s;
    outbuf_[out_offset_] = 0;
  }
  return res;
}

ssize_t HttpPipe::GetBody(int fd) {
  ssize_t n;
  while (content_length_ > 0 &&
         (n = read(fd, &outbuf_[0], outbuf_.capacity())) > 0)
    content_length_ -= n;

  return read(fd, &outbuf_[0], 1);  // should be EOF or EAGAIN
}

void HttpPipe::ParseURL(const char *url) {
  const char *s = strstr(url, "://");
  int scheme = 0;  // 0 for http
  if (s) {
    scheme = strncasecmp(url, "http", s - url);
    s += 3;
  } else {
    s = url;
  }

  switch (scheme) {
    case 0:
      if (sscanf(s, "%63[^:/]:%5[0-9]", host_, port_) == 1)
        strcpy(port_, "80");
      if (sscanf(s, "%*[^/]%1023s", path_) != 1)
        strcpy(path_, "/");
      break;
    default:
      errx(1, "unsupported scheme: %s", url);
  }
}

void HttpPipe::SetOutput(bool transferable, struct pollfd *pfd) {
  if (transferable) {
    pfd->events |= POLLOUT;
    if (pfd->fd == -1) {
      if (host_[0]) {
        errno = 0;
        pfd->fd = TcpNonBlockConnect(host_, port_);
        connected_ = errno == 0;
      } else {
        if ((pfd->fd = open(path_, O_APPEND)) < 0)
          err(1, "%s: open <%s> error", __func__, path_);
        NonBlocking(pfd->fd, 1);
      }
      http_flow_ = HTTP_REQUEST;
      if (request_state_ == HTTP_BODY) {  // restore
        assert(content_length_ > 0);
        assert(out_length_ > 0);
        out_length_ += out_offset_;
        out_offset_ = 0;
      }
      request_state_ = response_state_ = HTTP_HEAD;
    }
  } else {
    pfd->events &= ~POLLOUT;
  }
}

void HttpPipe::HandleInput(struct pollfd *pfd) {
  if (pfd->fd >= 0 && (pfd->revents & POLLIN)) {
    ssize_t n = ReadInput(pfd->fd);
    if (ILLEGAL(pfd->fd)) {
      warn("%s: ReadInput error", __func__);
      abort();
    } else if (n == 0) {
      warnx("%s: pipe input encounter EOF", __func__);
      pfd->fd = -1;
    }
  }
}

void HttpPipe::HandleOutput(struct pollfd *pfd, int *connect_retry) {
  if (host_[0]) {
    assert(header_);
    HandleHttpResponse(pfd);
    HandleHttpRequest(pfd);
    if (connected_)
      *connect_retry = 0;
  } else {
    assert(false);
  }
}

void HttpPipe::HandleHttpResponse(struct pollfd *pfd) {
  if (pfd->fd >= 0 && (pfd->revents & POLLIN)) {
    bool finished;
    ssize_t n = GetResponse(pfd->fd, &finished);
    if (finished)
      http_flow_ = HTTP_REQUEST;

    bool illegal = ILLEGAL(n);
    if (illegal)
      warn("%s: HttpPipe::GetResponse error", __func__);
    if (n == 0 || illegal)
      RESETFD(pfd->fd);
  }
}

void HttpPipe::HandleHttpRequest(struct pollfd *pfd) {
  if (pfd->fd >= 0 && (pfd->revents & POLLOUT)) {
    connected_ = true;
    bool finished;
    ssize_t n = SendRequest(pfd->fd, &finished);
    if (finished)
      http_flow_ = HTTP_RESPONSE;

    if (ILLEGAL(n)) {
      warn("%s: HttpPipe::SendRequest error", __func__);
      RESETFD(pfd->fd);
    }
  }
}

void HttpPipe::HandleError(struct pollfd *pfd, int *connect_retry) {
  if (pfd->fd >=0 && (pfd->revents & (POLLERR | POLLHUP))) {
    if (host_[0] && (pfd->revents & POLLERR)) {
      int sockerr = 0;
      socklen_t len = sizeof(sockerr);
      if (getsockopt(pfd->fd, SOL_SOCKET, SO_ERROR, &sockerr, &len))
        sockerr = errno;
      if (sockerr)
        warnx("%s: poll SO_ERROR: %s", __func__, strerror(sockerr));
      if (!connected_)
        ++*connect_retry;
    }
    RESETFD(pfd->fd);
  }
}

int HttpPipe::CheckTransfer(int *idle_transfer_n) {
  if (in_offset_ == 0 && in_length_ == 0 && http_flow_ == HTTP_REQUEST)
    return -1;

  if (http_flow_ == HTTP_RESPONSE)
    return 0;

  if (in_length_ > 0)
    return 1;

  if (in_offset_ == inbuf_.capacity() ||
      (in_offset_ > 0 && (*idle_transfer_n)++ < idle_transfer_)) {
    inbuf_.swap(outbuf_);
    in_length_ = in_offset_;
    in_offset_ = 0;
    return 1;
  }

  return 0;
}

}
