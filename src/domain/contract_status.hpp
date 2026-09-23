#pragma once

namespace inerxia::domain {

enum class ContractStatus { Active, Suspended, Overdue };

enum class SuspensionReason { Manual, Overdue };

}  // namespace inerxia::domain