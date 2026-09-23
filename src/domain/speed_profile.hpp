#pragma once

namespace inerxia::domain {

class SpeedProfile {
public:
    SpeedProfile(int download_mbps, int upload_mbps);

    [[nodiscard]] int download_mbps() const noexcept { return download_mbps_; }
    [[nodiscard]] int upload_mbps() const noexcept { return upload_mbps_; }

    friend bool operator==(const SpeedProfile&, const SpeedProfile&) = default;

private:
    int download_mbps_;
    int upload_mbps_;
};

}  // namespace inerxia::domain