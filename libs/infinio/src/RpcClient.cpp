/*
 * (C) Copyright 2015 ETH Zurich Systems Group (http://www.systems.ethz.ch/) and others.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Contributors:
 *     Markus Pilman <mpilman@inf.ethz.ch>
 *     Simon Loesing <sloesing@inf.ethz.ch>
 *     Thomas Etter <etterth@gmail.com>
 *     Kevin Bocksrocker <kevin.bocksrocker@gmail.com>
 *     Lucas Braun <braunl@inf.ethz.ch>
 */
#include <crossbow/infinio/RpcClient.hpp>

#include <crossbow/infinio/Fiber.hpp>

namespace crossbow {
namespace infinio {
namespace detail {

const std::error_code gEmptyErrorCode = std::error_code();

} // namespace detail

RpcResponse::~RpcResponse() = default;

bool RpcResponse::wait() {
    LOG_ASSERT(!mWaiting, "Result is already waiting");
    if (mPending) {
        mWaiting = true;
        mFiber.wait();
        mWaiting = false;
    }
    return !mPending;
}

void RpcResponse::notify() {
    if (mWaiting) {
        mFiber.resume();
    }
}

void RpcResponse::complete() {
    LOG_ASSERT(mPending, "Result has already completed");

    mPending = false;
    notify();
}

RpcClientSocket::RpcClientSocket(InfinibandSocket socket, size_t maxPendingResponses, size_t maxBatchSize)
        : Base(std::move(socket), maxBatchSize),
          mUserId(0x0u),
          mMaxPendingResponses(maxPendingResponses) {
    if (mMaxPendingResponses == 0x0u) {
        throw std::invalid_argument("Pending responses must be larger than 0");
    }
    mAsyncResponses.set_empty_key(std::numeric_limits<uint64_t>::max() - 1);
    mAsyncResponses.set_deleted_key(std::numeric_limits<uint64_t>::max());
}

void RpcClientSocket::onSocketConnected(const crossbow::string& data) {
    LOG_TRACE("Resuming waiting requests");
    mWaitingRequests.notify_all();
}

void RpcClientSocket::onSocketDisconnected() {
    mWaitingRequests.notify_all();

    while (!mSyncResponses.empty()) {
        auto response = std::move(std::get<1>(mSyncResponses.front()));
        mSyncResponses.pop();

        LOG_TRACE("Aborting waiting sync response");
        response->onAbort(std::make_error_code(std::errc::connection_aborted));
    }

    while (!mAsyncResponses.empty()) {
        auto i = mAsyncResponses.begin();
        auto response = std::move(i->second);
        mAsyncResponses.erase(i);

        LOG_TRACE("Aborting waiting async response");
        response->onAbort(std::make_error_code(std::errc::connection_aborted));
    }
}

void RpcClientSocket::onMessage(MessageId messageId, uint32_t messageType, crossbow::buffer_reader& message) {
    if (messageId.isAsync()) {
        onAsyncResponse(messageId.userId(), messageType, message);
    } else {
        onSyncResponse(messageId.userId(), messageType, message);
    }
}

void RpcClientSocket::onSyncResponse(uint32_t userId, uint32_t messageType, crossbow::buffer_reader& message) {
    while (!mSyncResponses.empty()) {
        uint32_t responseId;
        std::shared_ptr<RpcResponse> response;
        std::tie(responseId, response) = std::move(mSyncResponses.front());
        mSyncResponses.pop();

        mWaitingRequests.notify_one();

        if (userId != responseId) {
            LOG_TRACE("No response for transaction ID %1% received", responseId);
            response->onAbort(error::no_response);
            continue;
        }

        response->onResponse(messageType, message);
        return;
    }

    LOG_TRACE("Received message but no responses were waiting");
}

void RpcClientSocket::onAsyncResponse(uint32_t userId, uint32_t messageType, crossbow::buffer_reader& message) {
    auto i = mAsyncResponses.find(userId);
    if (i == mAsyncResponses.end()) {
        LOG_TRACE("Received message but no responses were waiting");
        return;
    }
    auto response = i->second;

    response->onResponse(messageType, message);
}

} // namespace infinio
} // namespace crossbow
