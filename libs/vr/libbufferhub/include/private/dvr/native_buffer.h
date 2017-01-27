#ifndef ANDROID_DVR_NATIVE_BUFFER_H_
#define ANDROID_DVR_NATIVE_BUFFER_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/native_window.h>
#include <base/logging.h>
#include <cutils/log.h>
#include <system/window.h>
#include <ui/ANativeObjectBase.h>
#include <utils/RefBase.h>

#include <private/dvr/buffer_hub_client.h>

namespace android {
namespace dvr {

// ANativeWindowBuffer is the abstraction Android HALs and frameworks use to
// pass around hardware graphics buffers. The following classes implement this
// abstraction with different DVR backing buffers, all of which provide
// different semantics on top of ion/gralloc buffers.

// An implementation of ANativeWindowBuffer backed by an IonBuffer.
class NativeBuffer
    : public android::ANativeObjectBase<ANativeWindowBuffer, NativeBuffer,
                                        android::LightRefBase<NativeBuffer>> {
 public:
  static constexpr int kEmptyFence = -1;

  explicit NativeBuffer(const std::shared_ptr<IonBuffer>& buffer)
      : BASE(), buffer_(buffer), fence_(kEmptyFence) {
    ANativeWindowBuffer::width = buffer->width();
    ANativeWindowBuffer::height = buffer->height();
    ANativeWindowBuffer::stride = buffer->stride();
    ANativeWindowBuffer::format = buffer->format();
    ANativeWindowBuffer::usage = buffer->usage();
    handle = buffer_->handle();
  }

  virtual ~NativeBuffer() {}

  std::shared_ptr<IonBuffer> buffer() { return buffer_; }
  int fence() const { return fence_.Get(); }

  void SetFence(int fence) { fence_.Reset(fence); }

 private:
  friend class android::LightRefBase<NativeBuffer>;

  std::shared_ptr<IonBuffer> buffer_;
  pdx::LocalHandle fence_;

  NativeBuffer(const NativeBuffer&) = delete;
  void operator=(NativeBuffer&) = delete;
};

// NativeBufferProducerSlice is an implementation of ANativeWindowBuffer backed
// by a buffer slice of a BufferProducer.
class NativeBufferProducerSlice
    : public android::ANativeObjectBase<
          ANativeWindowBuffer, NativeBufferProducerSlice,
          android::LightRefBase<NativeBufferProducerSlice>> {
 public:
  NativeBufferProducerSlice(const std::shared_ptr<BufferProducer>& buffer,
                            int buffer_index)
      : BASE(), buffer_(buffer) {
    ANativeWindowBuffer::width = buffer_->width();
    ANativeWindowBuffer::height = buffer_->height();
    ANativeWindowBuffer::stride = buffer_->stride();
    ANativeWindowBuffer::format = buffer_->format();
    ANativeWindowBuffer::usage = buffer_->usage();
    handle = buffer_->native_handle(buffer_index);
  }

  virtual ~NativeBufferProducerSlice() {}

 private:
  friend class android::LightRefBase<NativeBufferProducerSlice>;

  std::shared_ptr<BufferProducer> buffer_;

  NativeBufferProducerSlice(const NativeBufferProducerSlice&) = delete;
  void operator=(NativeBufferProducerSlice&) = delete;
};

// NativeBufferProducer is an implementation of ANativeWindowBuffer backed by a
// BufferProducer.
class NativeBufferProducer : public android::ANativeObjectBase<
  ANativeWindowBuffer, NativeBufferProducer,
  android::LightRefBase<NativeBufferProducer>> {
 public:
  static constexpr int kEmptyFence = -1;

  NativeBufferProducer(const std::shared_ptr<BufferProducer>& buffer,
                       EGLDisplay display, uint32_t surface_buffer_index)
      : BASE(),
        buffer_(buffer),
        surface_buffer_index_(surface_buffer_index),
        display_(display) {
    ANativeWindowBuffer::width = buffer_->width();
    ANativeWindowBuffer::height = buffer_->height();
    ANativeWindowBuffer::stride = buffer_->stride();
    ANativeWindowBuffer::format = buffer_->format();
    ANativeWindowBuffer::usage = buffer_->usage();
    handle = buffer_->native_handle();
    for (int i = 0; i < buffer->slice_count(); ++i) {
      // display == null means don't create an EGL image. This is used by our
      // Vulkan code.
      slices_.push_back(new NativeBufferProducerSlice(buffer, i));
      if (display_ != nullptr) {
        egl_images_.push_back(eglCreateImageKHR(
            display_, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
            static_cast<ANativeWindowBuffer*>(slices_.back().get()), nullptr));
        if (egl_images_.back() == EGL_NO_IMAGE_KHR) {
          ALOGE("NativeBufferProducer: eglCreateImageKHR failed");
        }
      }
    }
  }

  explicit NativeBufferProducer(const std::shared_ptr<BufferProducer>& buffer)
      : NativeBufferProducer(buffer, nullptr, 0) {}

  virtual ~NativeBufferProducer() {
    for (EGLImageKHR egl_image : egl_images_) {
      if (egl_image != EGL_NO_IMAGE_KHR)
        eglDestroyImageKHR(display_, egl_image);
    }
  }

  EGLImageKHR image_khr(int index) const { return egl_images_[index]; }
  std::shared_ptr<BufferProducer> buffer() const { return buffer_; }
  int release_fence() const { return release_fence_.Get(); }
  uint32_t surface_buffer_index() const { return surface_buffer_index_; }

  // Return the release fence, passing ownership to the caller.
  pdx::LocalHandle ClaimReleaseFence() { return std::move(release_fence_); }

  // Post the buffer consumer, closing the acquire and release fences.
  int Post(int acquire_fence, uint64_t sequence) {
    release_fence_.Close();
    return buffer_->Post(pdx::LocalHandle(acquire_fence), sequence);
  }

  // Gain the buffer producer, closing the previous release fence if valid.
  int Gain() { return buffer_->Gain(&release_fence_); }

  // Asynchronously gain the buffer, closing the previous release fence.
  int GainAsync() {
    release_fence_.Close();
    return buffer_->GainAsync();
  }

 private:
  friend class android::LightRefBase<NativeBufferProducer>;

  std::shared_ptr<BufferProducer> buffer_;
  pdx::LocalHandle release_fence_;
  std::vector<android::sp<NativeBufferProducerSlice>> slices_;
  std::vector<EGLImageKHR> egl_images_;
  uint32_t surface_buffer_index_;
  EGLDisplay display_;

  NativeBufferProducer(const NativeBufferProducer&) = delete;
  void operator=(NativeBufferProducer&) = delete;
};

// NativeBufferConsumer is an implementation of ANativeWindowBuffer backed by a
// BufferConsumer.
class NativeBufferConsumer : public android::ANativeObjectBase<
                                 ANativeWindowBuffer, NativeBufferConsumer,
                                 android::LightRefBase<NativeBufferConsumer>> {
 public:
  static constexpr int kEmptyFence = -1;

  explicit NativeBufferConsumer(const std::shared_ptr<BufferConsumer>& buffer,
                                int index)
      : BASE(), buffer_(buffer), acquire_fence_(kEmptyFence), sequence_(0) {
    ANativeWindowBuffer::width = buffer_->width();
    ANativeWindowBuffer::height = buffer_->height();
    ANativeWindowBuffer::stride = buffer_->stride();
    ANativeWindowBuffer::format = buffer_->format();
    ANativeWindowBuffer::usage = buffer_->usage();
    CHECK(buffer_->slice_count() > index);
    handle = buffer_->slice(index)->handle();
  }

  explicit NativeBufferConsumer(const std::shared_ptr<BufferConsumer>& buffer)
      : NativeBufferConsumer(buffer, 0) {}

  virtual ~NativeBufferConsumer() {}

  std::shared_ptr<BufferConsumer> buffer() const { return buffer_; }
  int acquire_fence() const { return acquire_fence_.Get(); }
  uint64_t sequence() const { return sequence_; }

  // Return the acquire fence, passing ownership to the caller.
  pdx::LocalHandle ClaimAcquireFence() { return std::move(acquire_fence_); }

  // Acquire the underlying buffer consumer, closing the previous acquire fence
  // if valid.
  int Acquire() { return buffer_->Acquire(&acquire_fence_, &sequence_); }

  // Release the buffer consumer, closing the acquire and release fences if
  // valid.
  int Release(int release_fence) {
    acquire_fence_.Close();
    sequence_ = 0;
    return buffer_->Release(pdx::LocalHandle(release_fence));
  }

 private:
  friend class android::LightRefBase<NativeBufferConsumer>;

  std::shared_ptr<BufferConsumer> buffer_;
  pdx::LocalHandle acquire_fence_;
  uint64_t sequence_;

  NativeBufferConsumer(const NativeBufferConsumer&) = delete;
  void operator=(NativeBufferConsumer&) = delete;
};

}  // namespace dvr
}  // namespace android

#endif  // ANDROID_DVR_NATIVE_BUFFER_H_
