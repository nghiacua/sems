#include "async_file.h"
#include "async_file_writer.h"
#include "log.h"

#define MIN_WRITE_SIZE 128*1024 /* 8 KB */

#include <event2/event_struct.h>

/*

Possible issue:

 once a file has been closed, it might take some time until it's
 data have been writen. It should be avoided to re-open this same
 file in the meantime.

 what can be done so that the exclusive-file container does not wait
 while the file gets closed & written?

*/

async_file::async_file(unsigned int buf_len)
  : AmMutex(true), fifo_buffer(buf_len),
    evbase(NULL),closed(false)
{
  evbase = async_file_writer::instance()->get_evbase();
  ev_write = event_new(evbase,-1,0,write_cb,this);
}

async_file::~async_file()
{
  event_free(ev_write);
  ev_write = NULL;
}

int async_file::write(const void* buf, unsigned int len)
{
  AmLock _l(*this);
  if(closed) return Closed;
  if(error)  return Error;

  int ret = fifo_buffer::write(buf,len);
  if(ret < 0) return BufferFull;

  if(fifo_buffer::get_buffered_bytes() >= MIN_WRITE_SIZE) {
    event_active(ev_write, 0, 0);
  }

  return ret;
}

int async_file::writev(const struct iovec *iov, int iovcnt)
{
  AmLock _l(*this);
  if(closed) return Closed;
  if(error)  return Error;

  int ret = fifo_buffer::writev(iov,iovcnt);
  if(ret < 0) return BufferFull;

  if(fifo_buffer::get_buffered_bytes() >= MIN_WRITE_SIZE) {
    event_active(ev_write, 0, 0);
  }

  return ret;
}

void async_file::close()
{
  AmLock _l(*this);
  closed = true;

  if(fifo_buffer::get_buffered_bytes())
    event_active(ev_write, 0, 0);
}

void async_file::write_cb(int sd, short what, void* ctx)
{
  ((async_file*)ctx)->write_cycle();
}

void async_file::write_cycle()
{
  int read_bs = 0;

  lock();
  read_bs = get_read_bs();
  unlock();

  while(read_bs > 0) {

    int bytes = write_to_file(get_read_ptr(),read_bs);
    if(bytes < 0) {
      if(errno == EINTR) continue;
      error  = true;
      ERROR("Error detected: stopped writing");
      break;
    }

    lock();
    skip(bytes);
    read_bs = get_read_bs();
    unlock();
  }

  lock();
  if(closed || error) {
    if(error || !fifo_buffer::get_buffered_bytes())
      on_flushed();
    else
      event_active(ev_write, 0, 0);
  }
  unlock();
}

unsigned int async_file::get_buffered_bytes()
{
  AmLock _l(*this);
  return fifo_buffer::get_buffered_bytes();
}