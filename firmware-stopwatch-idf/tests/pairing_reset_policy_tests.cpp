#include "../main/hal/pairing_reset_policy.h"

#include <cassert>
#include <iostream>

struct FakeStore {
    bool pending = false;
    bool clear_ok = true;
    bool verify_ok = true;
    bool acknowledge_ok = true;
    int security_records = 2;
    int subscription_records = 10;
    int app_settings = 7;
    int clear_calls = 0;
    int acknowledge_calls = 0;

    bool requested() const { return pending; }
    bool clear()
    {
        ++clear_calls;
        security_records = 0;
        if (!clear_ok) {
            return false;  // Simulate the old half-deleted security/CCCD store.
        }
        subscription_records = 0;
        return true;
    }
    bool empty() const { return verify_ok && security_records == 0 && subscription_records == 0; }
    bool acknowledge()
    {
        ++acknowledge_calls;
        if (!acknowledge_ok) {
            return false;
        }
        pending = false;
        return true;
    }
};

int main()
{
    using pairing_reset::complete_pending_reset;
    using pairing_reset::Result;

    FakeStore normal;
    assert(complete_pending_reset(normal) == Result::NotRequested);
    assert(normal.clear_calls == 0 && normal.security_records == 2 && normal.subscription_records == 10);

    FakeStore confirmed;
    confirmed.pending = true;
    assert(complete_pending_reset(confirmed) == Result::Completed);
    assert(!confirmed.pending && confirmed.security_records == 0 && confirmed.subscription_records == 0);
    assert(confirmed.app_settings == 7);
    assert(complete_pending_reset(confirmed) == Result::NotRequested);
    assert(confirmed.clear_calls == 1);

    FakeStore partial;
    partial.pending = true;
    partial.clear_ok = false;
    assert(complete_pending_reset(partial) == Result::ClearFailed);
    assert(partial.pending && partial.subscription_records == 10 && partial.acknowledge_calls == 0);
    partial.clear_ok = true;
    assert(complete_pending_reset(partial) == Result::Completed);
    assert(!partial.pending && partial.app_settings == 7);

    FakeStore unverified;
    unverified.pending = true;
    unverified.verify_ok = false;
    assert(complete_pending_reset(unverified) == Result::VerifyFailed);
    assert(unverified.pending && unverified.acknowledge_calls == 0);

    FakeStore commit_failed;
    commit_failed.pending = true;
    commit_failed.acknowledge_ok = false;
    assert(complete_pending_reset(commit_failed) == Result::AcknowledgeFailed);
    assert(commit_failed.pending);
    commit_failed.acknowledge_ok = true;
    assert(complete_pending_reset(commit_failed) == Result::Completed);

    std::cout << "PASS: normal boots preserve pairing; confirmed reset verifies clearing and retries partial failures\n";
}
