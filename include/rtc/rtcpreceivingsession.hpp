/**
 * Copyright (c) 2020 Staz Modrzynski
 * Copyright (c) 2020 Paul-Louis Ageneau
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. https://mozilla.org/MPL/2.0/
 */
#ifndef RTC_RTCP_RECEIVING_SESSION_H
#define RTC_RTCP_RECEIVING_SESSION_H
#if RTC_ENABLE_MEDIA
#include "common.hpp"
#include "mediahandler.hpp"
#include "message.hpp"
#include "rtp.hpp"
#include <atomic>
#include <array>
#include <chrono>
#include <mutex>

namespace rtc {
// One active RTP source per session. Rebuild consumers with this header/library pair.
class RTC_CPP_EXPORT RtcpReceivingSession : public MediaHandler {
public:
    // clockRate overrides SDP rates; 0 uses media() payload mappings (unknown rate reports jitter=0).
    explicit RtcpReceivingSession(uint32_t clockRate = 0);
    virtual ~RtcpReceivingSession() = default;
    void media(const Description::Media &description) override;
    void incoming(message_vector &messages, const message_callback &send) override;
    bool requestKeyframe(const message_callback &send) override;
    bool requestBitrate(unsigned int bitrate, const message_callback &send) override;
    [[deprecated("Use Track::requestKeyframe()")]] inline bool requestKeyframe() { return false; }
    [[deprecated("Use Track::requestBitrate()")]] inline void requestBitrate(unsigned int) {}
protected:
    using Clock = std::chrono::steady_clock;
    // Deterministic arrival seam for offline packet fixtures; no wall-clock timestamps.
    void incomingAt(message_vector &messages, const message_callback &send, Clock::time_point now);
    void pushRRAt(const message_callback &send, Clock::time_point now);
    void pushRR(const message_callback &send, unsigned int lastSrDelay);
    void pushREMB(const message_callback &send, unsigned int bitrate);
    void pushPLI(const message_callback &send);
private:
    void selectSource(SSRC ssrc); // callers hold mMutex
    void initSeq(uint16_t seq);
    bool updateSeq(uint16_t seq);
    message_ptr makeRR(Clock::time_point now, const uint32_t *delayOverride);
    std::mutex mMutex;
    const uint32_t mClockRate;
    std::array<uint32_t, 128> mPayloadClockRates{};
    uint32_t mActiveClockRate = 0;
    SSRC mReceiverSsrc;
    SSRC mSsrc = 0;
    bool mHaveSource = false, mHaveSeq = false, mHaveSr = false, mHaveTransit = false;
    uint16_t mMaxSeq = 0;
    uint32_t mBadSeq = 65537, mProbation = 2;
    uint64_t mCycles = 0, mBaseSeq = 0, mReceived = 0;
    uint64_t mExpectedPrior = 0, mReceivedPrior = 0;
    uint64_t mSyncNTPTS = 0;
    uint32_t mPreviousTimestamp = 0;
    uint64_t mJitter = 0; // RFC 3550 A.8 fixed-point estimator, 4 fractional bits
    Clock::time_point mPreviousArrival{}, mSrArrival{};
    std::atomic<unsigned int> mRequestedBitrate = 0;
};
} // namespace rtc
#endif
#endif
