#pragma once

#include "review.h"
#include "transport.h"

namespace td {
struct Request {
    bool registration;
    Policy policy;
    Digest tag{};
    std::vector<std::byte> psbt;
};

Request ParseRequest(const QRMessage& message);
Policy DefaultPolicy(const Keys& keys, unsigned purpose, unsigned account);
QRMessage PublicAccount(const Policy& policy, const Keys& keys);
std::optional<QRMessage> Register(const Policy& policy, const Keys& keys,
    const std::function<bool(const ReviewLines&)>& approve);
std::optional<QRMessage> Sign(Request request, const Keys& keys,
    const std::function<bool(const TransactionReview&)>& approve);
}
