#include "chunk.h"

#include <assert.h>
#include <netinet/in.h>
#include <string.h>

#include <algorithm>

namespace gaia {
namespace {

// Reinterprets a float directly as an int. This is useful for serializing float
// data.
// Args:
//  value: The float value that we want to reinterpret.
// Returns:
//  The integer representation of the float.
uint32_t AsInt(float value) { return *(reinterpret_cast<uint32_t *>(&value)); }

// Same as the above function, but does the opposite conversion.
float AsFloat(uint32_t value) { return *(reinterpret_cast<float *>(&value)); }
}

using ipc::Queue;
using ipc::Pool;

Chunk::Chunk(int size)
    : pool_(Pool::GetPool()),
      data_(new float[size]),
      gradients_(new float[size]),
      size_(size) {}

Chunk::Chunk(const float *data, const float *gradients, int size)
    : Chunk(size) {
  // Copy the initial data.
  SetValue(data, gradients);
}

Chunk::Chunk(const char *buffer) : pool_(Pool::GetPool()) {
  // Convert so we can access in 4-byte increments.
  const uint32_t *int_buffer = reinterpret_cast<const uint32_t *>(buffer);

  // Get the size.
  size_ = ntohl(int_buffer[0]);
  ++int_buffer;

  // Allocate arrays of that size.
  data_ = new float[size_];
  gradients_ = new float[size_];

  // Go through the buffer and use it to initialize the arrays.
  for (int i = 0; i < size_; ++i) {
    // Switch to host byte order.
    float data_value = AsFloat(ntohl(int_buffer[i]));
    float grad_value = AsFloat(ntohl(int_buffer[i]));

    // Copy into the arrays.
    data_[i] = data_value;
    gradients_[i] = grad_value;
  }
}

Chunk::~Chunk() {
  // Free the underlying data.
  delete[] data_;
  delete[] gradients_;
}

void Chunk::operator=(const Chunk &other) volatile {
  // Copy data.
  pool_ = other.pool_;
  size_ = other.size_;

  data_ = other.data_;
  gradients_ = other.gradients_;
}

void Chunk::SetValue(const float *data, const float *gradients) {
  memcpy(data_, data, size_ * sizeof(data[0]));
  memcpy(gradients_, gradients, size_ * sizeof(gradients[0]));
}

float *Chunk::GetData() const { return data_; }

float *Chunk::GetGradients() const { return gradients_; }

int Chunk::GetSerializedLength() const {
  const int elem_size = sizeof(data_[0]);

  return elem_size * size_ * 2  // Length of data + grad arrays.
         + sizeof(size_);       // Length of size_ member.
}

void Chunk::Serialize(char *buffer) const {
  // Convert so we can access in 4-byte increments.
  uint32_t *int_buffer = reinterpret_cast<uint32_t *>(buffer);

  // Copy the size.
  uint32_t reordered_size = htonl(size_);
  int_buffer[0] = reordered_size;
  ++int_buffer;

  // Go through the two arrays and copy everything into the buffer.
  for (int i = 0; i < size_; ++i) {
    // Switch to network byte order.
    uint32_t reordered_data = htonl(AsInt(data_[i]));
    uint32_t reordered_grad = htonl(AsInt(gradients_[i]));

    // Copy into the buffer.
    int_buffer[i] = reordered_data;
    int_buffer[i + size_] = reordered_grad;
  }
}

bool Chunk::Enqueue(Queue<Chunk> *queue) { return DoEnqueue(queue, false); }

void Chunk::EnqueueBlocking(Queue<Chunk> *queue) { DoEnqueue(queue, true); }

bool Chunk::Dequeue(Queue<Chunk> *queue) { return DoDequeue(queue, false); }

void Chunk::DequeueBlocking(Queue<Chunk> *queue) { DoDequeue(queue, true); }

bool Chunk::DoEnqueue(Queue<Chunk> *queue, bool block) {
  float *local_data = data_;
  float *local_gradients = gradients_;

  // Allocate space.
  data_ = pool_->AllocateForArray<float>(size_);
  gradients_ = pool_->AllocateForArray<float>(size_);

  // Copy stuff in.
  SetValue(local_data, local_gradients);

  // Enqueue it.
  bool success = true;
  if (!block) {
    success = queue->Enqueue(*this);
    if (!success) {
      // If it failed, we need to manually free the SHM.
      pool_->FreeArray<float>(data_, size_);
      pool_->FreeArray<float>(gradients_, size_);
    }
  } else {
    queue->EnqueueBlocking(*this);
  }

  // Set our arrays back to the local versions so we can still use this chunk
  // normally.
  data_ = local_data;
  gradients_ = local_gradients;

  return success;
}

bool Chunk::DoDequeue(Queue<Chunk> *queue, bool block) {
  // Save the local arrays, since dequeing will overwrite them.
  float *data = data_;
  float *gradients = gradients_;

  // Dequeue it.
  if (!block) {
    if (!queue->DequeueNext(this)) {
      return false;
    }
  } else {
    queue->DequeueNextBlocking(this);
  }

  ::std::swap(data_, data);
  ::std::swap(gradients_, gradients);

  // Copy it locally.
  SetValue(data, gradients);

  // Free the shared data, since we just dequeued it.
  pool_->FreeArray<float>(data, size_);
  pool_->FreeArray<float>(gradients, size_);

  return true;
}

}  // namespace gaia
