#pragma once

#include "domain/ids.hpp"
#include "domain/payment.hpp"

namespace inerxia::application {

class PaymentRepository {
public:
    virtual ~PaymentRepository() = default;

    virtual domain::PaymentId next_id() = 0;
    virtual void save(const domain::Payment&) = 0;
};

}  // namespace inerxia::application