// pipe.h
// Copyright 2014 <Vegertar, vegertar@gmail.com>

#ifndef PIPE_H_
#define PIPE_H_

#include <poll.h>
#include <stddef.h>
#include <sys/types.h>
#include <vector>

#define MAX_QUERY  2048

namespace v {

using std::vector;

class Header {
 public:
  virtual ~Header() {}
  virtual void SetRequest(const char *method,
                          const char *uri,
                          const char *ver) = 0;
  virtual void SetField(const char *field, const char *value) = 0;
  virtual const char * Generate(size_t body_size, size_t *head_size) = 0;
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

  int CheckTransfer(int *idle_transfer_n);
  ssize_t ReadInput(int fd);
  ssize_t SendRequest(int fd, bool *finished);
  ssize_t SendHead(int fd, size_t n);
  ssize_t SendBody(int fd, size_t n);
  ssize_t GetResponse(int fd, bool *finished);
  ssize_t GetHead(int fd);
  ssize_t GetBody(int fd);
  bool SetOutput(bool transferable, struct pollfd *pfd);
  bool HandleInput(struct pollfd *pfd);
  bool HandleOutput(struct pollfd *pfd);
  bool HandleHttpRequest(struct pollfd *pfd);
  bool HandleHttpResponse(struct pollfd *pfd);
  bool HandleError(struct pollfd *pfd);
  void ParseURL(const char *url);
  void Rollback();

  vector<char> inbuf_;
  vector<char> outbuf_;  // also for receiving http response
  vector<char> hdrbuf_;

  int buffer_size_;
  int connect_retry_;
  int idle_transfer_;
  bool *stop_flag_;
  int transfer_rate_;
  int zip_level_;
  Header *header_;

  size_t in_offset_;
  size_t in_length_;  // available data to transfer
  size_t out_offset_;
  size_t out_length_;  // data being transfer
  size_t hdr_offset_;
  size_t hdr_length_;
  size_t content_length_;
  size_t content_length_backup_;

  int infd_;
  char host_[64];
  char port_[6];
  char path_[1024];
  HttpState request_state_;
  HttpState response_state_;
  HttpFlow http_flow_;
};

}  // namespace v

#endif  // PIPE_H_
