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
#include <crossbow/string.hpp>
#include <crossbow/infinio/Endpoint.hpp>

#include "AddressHelper.hpp"

#include <cstring>

#include <arpa/inet.h>

namespace crossbow {
namespace infinio {

Endpoint::Endpoint() {
    memset(&mAddress, 0, sizeof(mAddress));
}

Endpoint::Endpoint(int family, const crossbow::string& host) {
    if (host.empty()) {
        return;
    }
    auto pos = host.find(':');
    auto hostPart = host.substr(0, pos);
    uint16_t portPart = std::stoul(host.substr(pos + 1).c_str());
    setAddress(family, hostPart, portPart);
}

Endpoint::Endpoint(int family, const crossbow::string& host, uint16_t port) {
    setAddress(family, host, port);
}

Endpoint::Endpoint(int family, uint16_t port) {
    switch (family) {
    case AF_INET: {
        memset(&mAddress.ipv4, 0, sizeof(mAddress.ipv4));
        mAddress.ipv4.sin_family = AF_INET;
        mAddress.ipv4.sin_port = htons(port);
    } break;
    case AF_INET6: {
        memset(&mAddress.ipv6, 0, sizeof(mAddress.ipv6));
        mAddress.ipv6.sin6_family = AF_INET6;
        mAddress.ipv6.sin6_port = htons(port);
    } break;
    default:
        break;
    }
}

Endpoint::Endpoint(sockaddr* addr) {
    switch (addr->sa_family) {
    case AF_INET: {
        memcpy(&mAddress.ipv4, addr, sizeof(struct sockaddr_in));
    } break;
    case AF_INET6: {
        memcpy(&mAddress.ipv6, addr, sizeof(struct sockaddr_in6));
    } break;
    default:
        break;
    }
}

void Endpoint::setAddress(int family, const string& host, uint16_t port) {
    switch (family) {
    case AF_INET: {
        memset(&mAddress.ipv4, 0, sizeof(mAddress.ipv4));
        mAddress.ipv4.sin_family = AF_INET;
        inet_pton(AF_INET, host.c_str(), &mAddress.ipv4.sin_addr);
        mAddress.ipv4.sin_port = htons(port);
    } break;
    case AF_INET6: {
        memset(&mAddress.ipv6, 0, sizeof(mAddress.ipv6));
        mAddress.ipv6.sin6_family = AF_INET6;
        inet_pton(AF_INET6, host.c_str(), &mAddress.ipv6.sin6_addr);
        mAddress.ipv6.sin6_port = htons(port);
    } break;
    default:
        break;
    }
}

crossbow::string Endpoint::getToken() const {
    return crossbow::string(formatAddress(reinterpret_cast<const struct sockaddr*>(&mAddress)));
}

std::ostream& operator<<(std::ostream& out, const Endpoint& rhs) {
    printAddress(out, reinterpret_cast<const struct sockaddr*>(&rhs.mAddress));
    return out;
}

} // namespace infinio
} // namespace crossbow
