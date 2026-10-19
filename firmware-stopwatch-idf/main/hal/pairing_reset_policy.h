#pragma once

namespace pairing_reset {

enum class Result {
    NotRequested,
    Completed,
    ClearFailed,
    VerifyFailed,
    AcknowledgeFailed,
};

// Run before NimBLE initializes/restores its RAM store. Keep the request
// pending after a partial failure so the next boot can safely retry it.
template <typename Store>
Result complete_pending_reset(Store& store)
{
    if (!store.requested()) {
        return Result::NotRequested;
    }
    if (!store.clear()) {
        return Result::ClearFailed;
    }
    if (!store.empty()) {
        return Result::VerifyFailed;
    }
    if (!store.acknowledge()) {
        return Result::AcknowledgeFailed;
    }
    return Result::Completed;
}

}  // namespace pairing_reset
