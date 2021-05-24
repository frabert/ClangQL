#ifndef IRESULTSTREAM_HPP
#define IRESULTSTREAM_HPP

template <typename T> class IResultStream {
public:
  virtual ~IResultStream() = default;

  virtual const T &Current() = 0;
  virtual bool Next() = 0;
};

#endif