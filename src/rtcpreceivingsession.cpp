/**
 * Copyright (c) 2020 Staz Modrzynski
 * Copyright (c) 2020 Paul-Louis Ageneau
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. https://mozilla.org/MPL/2.0/
 */
#if RTC_ENABLE_MEDIA
#include "rtcpreceivingsession.hpp"
#include "impl/utils.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rtc {
namespace {
uint32_t elapsedTicks(std::chrono::steady_clock::duration elapsed, uint32_t rate) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    if (ns <= 0 || rate == 0) return 0;
    const uint64_t seconds = uint64_t(ns) / 1000000000;
    if (seconds > UINT32_MAX / rate) return UINT32_MAX;
    const uint64_t ticks = seconds * rate + (uint64_t(ns) % 1000000000) * rate / 1000000000;
    return uint32_t(std::min<uint64_t>(ticks, UINT32_MAX));
}
uint8_t u8(byte b) { return std::to_integer<uint8_t>(b); }
uint16_t be16(const byte *p) { return uint16_t((u8(p[0]) << 8) | u8(p[1])); }
bool validRtp(const Message &m) {
    if (m.size() < 12 || (u8(m[0]) >> 6) != 2) return false;
    // RTCP mux reserves the full second-byte range 192..223 (including marker).
    if (u8(m[1]) >= 192 && u8(m[1]) <= 223) return false;
    size_t h = 12 + 4 * (u8(m[0]) & 15);
    if (h > m.size()) return false;
    if (u8(m[0]) & 16) {
        if (m.size() - h < 4) return false;
        const size_t ext = 4 + 4 * size_t(be16(m.data() + h + 2));
        if (ext > m.size() - h) return false;
        h += ext;
    }
    if ((u8(m[0]) & 32) && (u8(m.back()) == 0 || u8(m.back()) > m.size() - h)) return false;
    return true;
}
bool validRtcp(const Message &m) {
    if (m.empty()) return false;
    for (size_t p = 0; p < m.size();) {
        if (m.size() - p < 4 || (u8(m[p]) >> 6) != 2) return false;
        const size_t n = (size_t(be16(m.data() + p + 2)) + 1) * 4;
        if (n > m.size() - p) return false;
        size_t usable = n;
        if (u8(m[p]) & 32) {
            const size_t pad = u8(m[p + n - 1]);
            if (p + n != m.size() || pad == 0 || pad > n - 4) return false;
            usable -= pad;
        }
        const size_t blocks = (u8(m[p]) & 31) * 24;
        if (u8(m[p + 1]) == 200 && usable < 28 + blocks) return false;
        if (u8(m[p + 1]) == 201 && usable < 8 + blocks) return false;
        p += n;
    }
    return true;
}
} // namespace

RtcpReceivingSession::RtcpReceivingSession(uint32_t clockRate)
    : mClockRate(clockRate), mReceiverSsrc(impl::utils::random_engine()()) {
    if (clockRate > 1000000000) throw std::invalid_argument("Invalid RTP clock rate");
}
void RtcpReceivingSession::media(const Description::Media &description) {
    std::lock_guard lock(mMutex);
    mPayloadClockRates.fill(0);
    for (int payload : description.payloadTypes()) {
        const auto *map = description.rtpMap(payload);
        if (payload >= 0 && payload < 128 && map && map->clockRate > 0 && map->clockRate <= 1000000000)
            mPayloadClockRates[size_t(payload)] = uint32_t(map->clockRate);
    }
}
void RtcpReceivingSession::selectSource(SSRC ssrc) {
    if (mHaveSource && mSsrc == ssrc) return;
    mHaveSource = true; mSsrc = ssrc;
    // Keep the local reporting identity distinct from the remote source.
    if (mReceiverSsrc == ssrc) mReceiverSsrc ^= 0x80000000u;
    mHaveSeq = mHaveSr = mHaveTransit = false; mActiveClockRate = 0;
    mProbation = 2; mBadSeq = 65537; mCycles = mBaseSeq = mReceived = 0;
    mExpectedPrior = mReceivedPrior = mSyncNTPTS = 0; mJitter = 0;
}
void RtcpReceivingSession::initSeq(uint16_t seq) {
    mBaseSeq = mMaxSeq = seq; mCycles = mReceived = 0; mBadSeq = 65537;
    mExpectedPrior = mReceivedPrior = 0; mHaveTransit = false; mJitter = 0;
}
bool RtcpReceivingSession::updateSeq(uint16_t seq) {
    // RFC 3550 Appendix A.1: two sequential packets establish a source.
    if (!mHaveSeq) { mHaveSeq = true; mMaxSeq = seq; mProbation = 1; return false; }
    if (mProbation) {
        if (seq == uint16_t(mMaxSeq + 1)) {
            mMaxSeq = seq;
            if (--mProbation == 0) { initSeq(seq); ++mReceived; return true; }
        } else { mProbation = 1; mMaxSeq = seq; }
        return false;
    }
    const uint16_t delta = uint16_t(seq - mMaxSeq);
    if (delta < 3000) {
        if (seq < mMaxSeq) mCycles += 65536;
        mMaxSeq = seq;
    } else if (delta <= 65536 - 100) {
        if (seq != mBadSeq) { mBadSeq = uint16_t(seq + 1); return false; }
        initSeq(seq); mHaveSr = false; mSyncNTPTS = 0;
    } // else: duplicate/reordered packet; count it without moving the highest sequence.
    ++mReceived;
    return true;
}
void RtcpReceivingSession::incoming(message_vector &messages, const message_callback &send) {
    incomingAt(messages, send, Clock::now());
}
void RtcpReceivingSession::incomingAt(message_vector &messages, const message_callback &send,
                                      Clock::time_point now) {
    message_vector result;
    for (auto &message : messages) {
        if (!message) continue;
        if (message->type == Message::Binary) {
            if (!validRtp(*message)) continue;
            const auto *rtp = reinterpret_cast<const RtpHeader *>(message->data());
            {
                std::lock_guard lock(mMutex);
                selectSource(rtp->ssrc());
                const uint32_t rate = mClockRate ? mClockRate : mPayloadClockRates[rtp->payloadType()];
                if (rate != mActiveClockRate) { mActiveClockRate = rate; mHaveTransit = false; mJitter = 0; }
                if (updateSeq(rtp->seqNumber()) && rate) {
                    const uint32_t ts = rtp->timestamp();
                    if (mHaveTransit) {
                        const uint32_t raw = ts - mPreviousTimestamp;
                        const int64_t delta = raw <= INT32_MAX ? int64_t(raw) : int64_t(raw) - 4294967296LL;
                        const int64_t difference = int64_t(elapsedTicks(now - mPreviousArrival, rate)) - delta;
                        const uint64_t distance = uint64_t(difference < 0 ? -difference : difference);
                        mJitter = mJitter + distance - ((mJitter + 8) >> 4);
                    }
                    mHaveTransit = true; mPreviousTimestamp = ts; mPreviousArrival = now;
                }
            }
            // This handler measures reception; probation/jump validation does not filter media.
            result.push_back(std::move(message));
        } else if (message->type == Message::Control && validRtcp(*message)) {
            for (size_t p = 0; p < message->size();) {
                const auto *header = reinterpret_cast<const RtcpHeader *>(message->data() + p);
                if (header->payloadType() == 200) {
                    const auto *sr = reinterpret_cast<const RtcpSr *>(message->data() + p);
                    bool matching = false;
                    {
                        std::lock_guard lock(mMutex);
                        if (!mHaveSource) selectSource(sr->senderSSRC());
                        matching = mSsrc == sr->senderSSRC();
                        if (matching) { mSyncNTPTS = sr->ntpTimestamp(); mSrArrival = now; mHaveSr = true; }
                    }
                    if (matching) {
                        pushRRAt(send, now);
                        if (auto bitrate = mRequestedBitrate.load()) pushREMB(send, bitrate);
                    }
                }
                p += header->lengthInBytes();
            }
        }
    }
    messages.swap(result);
}
message_ptr RtcpReceivingSession::makeRR(Clock::time_point now, const uint32_t *delayOverride) {
    std::lock_guard lock(mMutex);
    const bool report = mHaveSeq && mProbation == 0;
    auto message = make_message(RtcpRr::SizeWithReportBlocks(report ? 1 : 0), Message::Control);
    auto *rr = reinterpret_cast<RtcpRr *>(message->data());
    rr->preparePacket(mReceiverSsrc, report ? 1 : 0);
    if (!report) return message;
    const uint64_t highest = mCycles + mMaxSeq;
    const uint64_t expected = highest - mBaseSeq + 1;
    const int64_t lost = int64_t(expected) - int64_t(mReceived);
    const uint64_t expectedInterval = expected - mExpectedPrior;
    const int64_t lostInterval = int64_t(expectedInterval) - int64_t(mReceived - mReceivedPrior);
    const uint8_t fraction = expectedInterval && lostInterval > 0
        ? uint8_t(std::min<uint64_t>(255, (uint64_t(lostInterval) * 256) / expectedInterval)) : 0;
    mExpectedPrior = expected; mReceivedPrior = mReceived;
    uint32_t delay = 0;
    if (mHaveSr) {
        delay = delayOverride ? *delayOverride : elapsedTicks(now - mSrArrival, 65536);
    }
    rr->getReportBlock(0)->prepareReceptionReport(mSsrc, fraction,
        int32_t(std::clamp<int64_t>(lost, -8388608, 8388607)), uint32_t(highest),
        uint32_t(std::min<uint64_t>(mJitter >> 4, UINT32_MAX)), mHaveSr ? mSyncNTPTS : 0, delay);
    return message;
}
void RtcpReceivingSession::pushRRAt(const message_callback &send, Clock::time_point now) { send(makeRR(now, nullptr)); }
void RtcpReceivingSession::pushRR(const message_callback &send, unsigned int lastSrDelay) { send(makeRR(Clock::now(), &lastSrDelay)); }
bool RtcpReceivingSession::requestBitrate(unsigned int bitrate, const message_callback &send) {
    mRequestedBitrate.store(bitrate); pushREMB(send, bitrate); return true;
}
void RtcpReceivingSession::pushREMB(const message_callback &send, unsigned int bitrate) {
    SSRC target, local;
    { std::lock_guard lock(mMutex); target = mSsrc; local = mReceiverSsrc; }
    auto message = make_message(RtcpRemb::SizeWithSSRCs(1), Message::Control);
    auto *remb = reinterpret_cast<RtcpRemb *>(message->data());
    remb->preparePacket(local, 1, bitrate); remb->setSsrc(0, target); send(message);
}
bool RtcpReceivingSession::requestKeyframe(const message_callback &send) { pushPLI(send); return true; }
void RtcpReceivingSession::pushPLI(const message_callback &send) {
    SSRC target, local;
    { std::lock_guard lock(mMutex); target = mSsrc; local = mReceiverSsrc; }
    auto message = make_message(RtcpPli::Size(), Message::Control);
    auto *pli = reinterpret_cast<RtcpPli *>(message->data());
    pli->preparePacket(target); pli->header.setPacketSenderSSRC(local); send(message);
}
} // namespace rtc
#endif
