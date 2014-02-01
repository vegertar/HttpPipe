// pipe.h

#ifndef _PIPE_H
#define _PIPE_H

#include <poll.h>
#include <stddef.h>
#include <sys/types.h>
#include <vector>

namespace v {

using std::vector;

class Header {
 public:
  virtual ~Header() {}
  virtual void SetRequest(const char *method,
                          const char *uri,
                          const char *ver) = 0;
  virtual void SetField(const char *field, const char *value) = 0;
  virtual const char * Generate(long content_length, long *header_length) = 0;
};

class HttpPipe {
 public:
  HttpPipe();

  void Init(int infd, const char *outurl);
  void Serve(int timeout);

  // Setting methods:
  //   set property and returns previous one
  //   specially, the parameter -1/NULL do not change the value
  int SetBufferSize(int n);
  int SetConnectRetry(int n);
  bool * SetStopFlag(bool *p);
  int SetIdleTransfer(int n);
  int SetTransferRate(int n);
  int SetZipLevel(int n);
  Header * SetHeader(Header *p);

 private:
  enum HttpState { HTTP_HEAD, HTTP_BODY };
  enum HttpFlow { HTTP_REQUEST, HTTP_RESPONSE };

  ssize_t ReadInput(int fd);
  ssize_t SendRequest(int fd, bool *finished);
  ssize_t SendHead(int fd, size_t n);
  ssize_t SendBody(int fd, size_t n);
  ssize_t GetResponse(int fd, bool *finished);
  ssize_t GetHead(int fd);
  ssize_t GetBody(int fd);
  void ParseURL(const char *url);
  void SetOutput(bool transferable, struct pollfd *pfd);
  void HandleInput(struct pollfd *pfd);
  void HandleOutput(struct pollfd *pfd, int *connect_retry);
  void HandleHttpRequest(struct pollfd *pfd);
  void HandleHttpResponse(struct pollfd *pfd);
  void HandleError(struct pollfd *pfd, int *connect_retry);
  int CheckTransfer(int *idle_transfer_n);

  vector<char> inbuf_;
  vector<char> outbuf_;  // also for receive http response

  int buffer_size_;
  int connect_retry_;
  int idle_transfer_;
  bool *stop_flag_;
  int transfer_rate_;
  int zip_level_;
  Header *header_;

  size_t in_offset_;
  size_t in_length_;
  size_t out_offset_;
  size_t out_length_;
  size_t content_length_;

  int infd_;
  char host_[64];
  char port_[6];
  char path_[1024];
  bool connected_;
  HttpState request_state_;
  HttpState response_state_;
  HttpFlow http_flow_;
};

}  // namespace v

#endif  // !_PIPE_H
