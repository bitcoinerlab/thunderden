#pragma once

#include "application.h"
#include "review.h"

namespace td {
// Only public data crosses this boundary. Each callback is local to the signer.
struct QRApproval {
    std::function<bool(const ReviewLines&)> export_key;
    std::function<bool(const ReviewLines&)> register_wallet;
    std::function<bool(const ReviewLines&)> address;
    std::function<bool(const TransactionReview&)> transaction;
};

Digest KeyIdentity(const Keys& keys);
QRMessage HandleQRRequest(const QRMessage& message, const Keys& keys, const QRApproval& approve);
}
