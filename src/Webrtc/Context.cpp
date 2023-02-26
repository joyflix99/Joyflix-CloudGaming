#pragma clang diagnostic ignored "-Wunreachable-code"
#pragma clang diagnostic ignored "-Wunused-variable"
#include "Context.h"

#include <iostream>

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/task_queue/default_task_queue_factory.h"

namespace webrtc {

using namespace webrtc;

Context::Context()
    : m_workerThread(rtc::Thread::CreateWithSocketServer()),
      m_signalingThread(rtc::Thread::CreateWithSocketServer()),
      m_taskQueueFactory(CreateDefaultTaskQueueFactory()) {
  m_workerThread->Start();
  m_signalingThread->Start();

  rtc::InitializeSSL();

  m_peerConnectionFactory = webrtc::CreatePeerConnectionFactory(
      m_workerThread.get() /* network_thread */,
      m_workerThread.get() /* worker_thread */, m_signalingThread.get(),
      nullptr /* default_adm */,
      CreateBuiltinAudioEncoderFactory(),CreateBuiltinAudioDecoderFactory(),
      CreateBuiltinVideoEncoderFactory(),CreateBuiltinVideoDecoderFactory(),
      nullptr /* audio_mixer */,
      nullptr /* audio_processing */);

  if (!m_peerConnectionFactory) {
    std::cout << "Failed to initialize PeerConnectionFactory" << std::endl;
  }
}

Context::~Context(){
    {
        std::lock_guard<std::mutex> lock(mutex);

        m_peerConnectionFactory = nullptr;
//        m_workerThread->BlockingCall([this]() { m_audioDevice = nullptr; });
        m_mapClients.clear();

        // check count of refptr to avoid to forget disposing
        RTC_DCHECK_EQ(m_mapRefPtr.size(), 0);

        m_mapRefPtr.clear();
        m_mapMediaStreamObserver.clear();
//        m_mapDataChannels.clear();
//        m_mapVideoRenderer.clear();

        m_workerThread->Quit();
        m_workerThread.reset();
        m_signalingThread->Quit();
        m_signalingThread.reset();
    }
}

void Context::AddObserver(
    const webrtc::PeerConnectionInterface* connection,
    const rtc::scoped_refptr<SetSessionDescriptionObserver>& observer) {
  m_mapSetSessionDescriptionObserver[connection] = observer;
}

SetSessionDescriptionObserver* Context::GetObserver(
    webrtc::PeerConnectionInterface* connection) {
      return m_mapSetSessionDescriptionObserver[connection].get();
    }

PeerConnectionObject* Context::CreatePeerConnection(
    const webrtc::PeerConnectionInterface::RTCConfiguration& config) {
    std::unique_ptr<PeerConnectionObject> obj = std::make_unique<PeerConnectionObject>(*this);
    PeerConnectionDependencies dependencies(obj.get());
    auto result = m_peerConnectionFactory->CreatePeerConnectionOrError(config, std::move(dependencies));
    if (!result.ok())
    {
        RTC_LOG(LS_ERROR) << result.error().message();
        return nullptr;
    }
    obj->connection = result.MoveValue();
    PeerConnectionObject* ptr = obj.get();
    m_mapClients[ptr] = std::move(obj);
    return ptr;
}

void Context::AddTracks() {
  for (auto&& kv : m_mapClients) {
    kv.first->AddTracks(m_peerConnectionFactory.get());
  }
}

}  // namespace webrtc