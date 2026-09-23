#include "domain/speed_profile.hpp"

#include "domain/domain_error.hpp"

namespace inerxia::domain {

SpeedProfile::SpeedProfile(int download_mbps, int upload_mbps)
    : download_mbps_(download_mbps), upload_mbps_(upload_mbps) {
    if (download_mbps_ <= 0 || upload_mbps_ <= 0) {
        throw DomainError(
            "Speed profile must have positive download and upload rates");
    }
}

}  // namespace inerxia::domain