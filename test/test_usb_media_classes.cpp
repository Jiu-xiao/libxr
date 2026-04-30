#include <cstddef>
#include <cstdint>

#include "ep_pool.hpp"
#include "test.hpp"
#include "uac_speaker.hpp"
#include "uvc_camera.hpp"

namespace
{
class MockUsbEndpoint : public LibXR::USB::Endpoint
{
 public:
  MockUsbEndpoint(EPNumber number, Direction direction, LibXR::RawData buffer)
      : Endpoint(number, direction, buffer)
  {
  }

  void Configure(const Config& cfg) override
  {
    GetConfig() = cfg;
    SetState(State::IDLE);
  }

  void Close() override { SetState(State::DISABLED); }
  LibXR::ErrorCode Stall() override
  {
    SetState(State::STALLED);
    return LibXR::ErrorCode::OK;
  }
  LibXR::ErrorCode ClearStall() override
  {
    SetState(State::IDLE);
    return LibXR::ErrorCode::OK;
  }
  LibXR::ErrorCode Transfer(size_t) override
  {
    SetState(State::BUSY);
    return LibXR::ErrorCode::OK;
  }
};
}  // namespace

void test_usb_media_classes()
{
  uint8_t ep1_buffer[2048] = {};
  uint8_t ep2_buffer[2048] = {};
  MockUsbEndpoint ep1(LibXR::USB::Endpoint::EPNumber::EP1,
                      LibXR::USB::Endpoint::Direction::BOTH,
                      {ep1_buffer, sizeof(ep1_buffer)});
  MockUsbEndpoint ep2(LibXR::USB::Endpoint::EPNumber::EP2,
                      LibXR::USB::Endpoint::Direction::BOTH,
                      {ep2_buffer, sizeof(ep2_buffer)});
  LibXR::USB::EndpointPool pool(4);
  ASSERT(pool.Put(&ep1) == LibXR::ErrorCode::OK);
  ASSERT(pool.Put(&ep2) == LibXR::ErrorCode::OK);

  LibXR::USB::UAC1SpeakerQ<2, 16> speaker(48000);
  uint8_t pcm[4] = {};
  ASSERT(speaker.QueueSize() == 0);
  ASSERT(speaker.ReadPcm(pcm, sizeof(pcm)) == LibXR::ErrorCode::EMPTY);
  speaker.BindEndpoints(pool, 0, false);
  uint8_t alt = 0xFF;
  ASSERT(speaker.GetAltSetting(1, alt) == LibXR::ErrorCode::OK);
  ASSERT(alt == 0);
  ASSERT(speaker.SetAltSetting(1, 1) == LibXR::ErrorCode::OK);
  ASSERT(speaker.GetAltSetting(1, alt) == LibXR::ErrorCode::OK);
  ASSERT(alt == 1);

  using Camera = LibXR::USB::UVCCamera<1, 1, 2>;
  static constexpr auto rgb565 = Camera::FourCcGuid('R', 'G', 'B', 'P');
  static constexpr uint32_t intervals[] = {333333, 666666};
  static constexpr Camera::FrameSpec frames[] = {
      {160, 120, intervals, 2, intervals[0], 160u * 120u * 2u}};
  static constexpr Camera::FormatSpec formats[] = {{rgb565.bytes, 16, frames, 1, 1}};

  Camera camera(formats, 1);
  camera.BindEndpoints(pool, 2, false);
  ASSERT(!camera.IsStreaming());
  ASSERT(!camera.IsFrameBusy());
  ASSERT(camera.CurrentFormatIndex() == 1);
  ASSERT(camera.CurrentFrameIndex() == 1);
  ASSERT(camera.CurrentFrameInterval100ns() == intervals[0]);
  ASSERT(camera.SubmitFrame({pcm, sizeof(pcm)}) == LibXR::ErrorCode::STATE_ERR);
  ASSERT(camera.SetAltSetting(3, 1) == LibXR::ErrorCode::OK);
  ASSERT(camera.SubmitFrame({pcm, sizeof(pcm)}) == LibXR::ErrorCode::OK);

  camera.UnbindEndpoints(pool, false);
  speaker.UnbindEndpoints(pool, false);
}
