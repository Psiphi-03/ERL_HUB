# ERL-HUB (`ERL_HUB2.cpp`) Documentation

## Overview
**ERL-HUB** is a C++ command-line emergency relief logistics management application. It models depot inventory management, cargo allocation (volume-based, medical-priority, and emergency triage overrides), multi-vehicle fleet registration and dispatch with route/payload validation, urgent medical need prioritization, logistics statistics, and persistent binary audit logging.

---

## Architecture & Namespaces

The program organizes its core domain types into two nested namespaces under `ReliefOps`:

* **`ReliefOps::Inventory`**: Manages relief items (`ReliefCargo`) and depot stock (`DistributionDepot`).
* **`ReliefOps::Logistics`**: Handles allocation routines, custom exceptions, commander overrides, vehicle hierarchies, dispatch queues, stream formatting, and binary audit persistence.
* **Global Scope**: Provides validated console input helpers (`readInt`, `readDouble`) and the interactive `main()` menu loop.

---

## Core Classes, Structs & Templates

### `ReliefOps::Inventory::ReliefCargo`
Represents an individual supply item or cargo batch.
* **Fields**: `name_` (`std::string`), `quantity_` (`int`), `unitWeight_` (`double`, kg/unit), `unitVolume_` (`double`, $\text{m}^3$/unit).
* **Key Methods & Operators**:
  * Constructor with defaults: `ReliefCargo(std::string name = "", int q = 0, double weight = 0, double volume = 0)`.
  * Accessors & mutator: `name()`, `quantity()`, `unitWeight()`, `unitVolume()`, `setQuantity(int q)`.
  * `operator==`: Compares cargo items by `name_`.
  * `operator<<` / `operator>>`: Space-delimited serialization and deserialization (`name quantity unitWeight unitVolume`).
* **Friends**: Grants access to `DistributionDepot` and `ReliefOps::Logistics::IncidentCommander`.

### `ReliefOps::Inventory::DistributionDepot`
Stores and updates a collection of `ReliefCargo` items (`stock_`).
* **Key Methods & Operators**:
  * `stockCount(const std::string& name)`: Returns a mutable `int&` to the item's quantity, or throws `std::out_of_range("Supply not found in depot")` if absent.
  * `items()`: Returns a `const std::vector<ReliefCargo>&` of depot stock.
  * `operator+=`: Adds quantity to an existing matching item or appends a new `ReliefCargo` entry.
  * `operator-=`: Deducts cargo quantity from stock, throwing `std::runtime_error("Insufficient depot stock")` if insufficient.
* **Friend**: Grants access to `ReliefOps::Logistics::IncidentCommander`.

### `ReliefOps::Logistics::IncidentCommander`
Provides privileged operational overrides.
* `static ReliefCargo& criticalTriageOverride(DistributionDepot& depot, ReliefCargo& mission, int units)`: Deducts up to `units` directly from `depot.stock_`, updates `mission.quantity_`, and returns `mission` (or throws `std::out_of_range("Emergency supply not found")`).

### `ReliefOps::Logistics::TransportVehicle`
Base class managing a dynamically allocated cargo manifest array (`manifest_` of size `slots_`, currently holding `used_` items) and fleet-wide counters (`activeVehicles_`, `dispatchedTonnes_`).
* **Memory & Copy Semantics**:
  * Allocates `manifest_` in the constructor and frees it via `delete[]` in the virtual destructor, updating `activeVehicles_`.
  * Implements a deep-copy constructor (`TransportVehicle(const TransportVehicle&)`).
  * Deletes the copy assignment operator (`operator=`).
* **Manifest & Dispatch Methods**:
  * `load(const ReliefCargo& cargo)`: Appends cargo if `used_ < slots_`; returns `false` when full.
  * `item(std::size_t i)` / `itemCount()`: Read-only item access and current slot usage count.
  * `manifestWeightKg()`: Sums `payloadKg(manifest_[i])` across loaded items.
  * `hasDuplicateCargo()`: Checks for duplicate cargo names (`operator==`) within the manifest.
  * `operator==`: Compares manifest item count and element-wise equality.
  * `dispatch(double tonnes)`: Adds `tonnes` to `dispatchedTonnes_`.
  * Static getters: `activeVehicles()`, `dispatchedTonnes()`.
  * `operator<<` / `operator>>`: Serializes/deserializes `used_` followed by each `ReliefCargo` record.

### Composed & Derived Vehicle Hierarchy
* **Composed Subsystems**:
  * `GPSBeacon`: Tracks destination zone (`setZone`, `zone()`, defaulting to `"Unassigned"`).
  * `EmergencyLifeSupportUnit`: Tracks reserve minutes (`reserveMinutes_ = 180`) and readiness (`ready()`).
* **`ReliefVehicle` (inherits `TransportVehicle`)**:
  * Adds `id_`, `capacityKg_`, `fuel_` (default `100`), `damagedCorridor_` (default `false`), `gps_`, and `lifeSupport_`.
  * `checkRoute()`: Validates `manifestWeightKg() <= capacityKg_`, `fuel_ > 0`, and `!damagedCorridor_`, throwing `CargoOverweightException`, `FuelDepletionException`, or `DamagedCorridorException`.
  * Pure virtual interface: `evaluateRoute() const = 0` and `type() const = 0`.
* **Concrete & Virtual Inheritance Variants**:
  * `AerialTransport` (virtually inherits `ReliefVehicle`; default `cap=500`, `slots=4`, `id="HELI-1"`).
  * `GroundConvoy` (virtually inherits `ReliefVehicle`; default `cap=2000`, `slots=4`, `id="TRUCK-1"`).
  * `AmphibiousCraft` (virtually inherits `ReliefVehicle`; default `cap=3000`, `slots=4`, `id="BOAT-1"`).
  * `HybridMedicalUnit` (inherits `public GroundConvoy, public AmphibiousCraft`; virtual base avoids duplicate `ReliefVehicle` subobjects; default `cap=2500`, `slots=4`, `id="HYBRID-1"`).

### Medical Prioritization & Queue Template
* `struct MedicalNeed`: Holds `item` (`std::string`) and `priority` (`int`).
* `struct UrgencyFirst`: Comparator (`a.priority > b.priority`) ordering lower priority numbers as higher urgency in `std::priority_queue` / `std::sort`.
* `template<class T, class Compare = std::less<T>> class PriorityDispatchQueue`: Wrapper around `std::priority_queue<T, std::vector<T>, Compare>` exposing `push`, `pop`, `top`, and `empty`.

---

## Functions, Exceptions & I/O Utilities

### Custom Exceptions (`ReliefOps::Logistics`)
* `CargoOverweightException`: `"Cargo exceeds vehicle payload capacity"`
* `FuelDepletionException`: `"Vehicle has insufficient fuel"`
* `DamagedCorridorException`: `"Planned route is damaged"`

### Allocation & Geometry Helpers (`ReliefOps::Logistics`)
* `distanceKm(double x1, double y1, double x2, double y2)`: Euclidean distance $\sqrt{(x_2-x_1)^2 + (y_2-y_1)^2}$.
* `payloadKg(const ReliefCargo& c)`: Returns `c.quantity() * c.unitWeight()`.
* `allocate(DistributionDepot& depot, ReliefCargo& mission, double& freeVolume)`: Caps allocation by `mission.quantity()`, available depot stock, and available volume (`freeVolume / mission.unitVolume()`), deducting stock and `freeVolume` and updating `mission.quantity_`.
* `allocate(DistributionDepot& depot, ReliefCargo& mission, int& patients, int priority)`: Requests all `patients` when `priority >= 8`, or `(patients + 1) / 2` otherwise; caps by available stock, and updates both `patients` and `mission.quantity_`.

### Stream Manipulator & Audit File I/O (`ReliefOps::Logistics`)
* `manifestTable(std::ostream& out)`: Formats a table header (`SUPPLY`, `QUANTITY`, `KG/UNIT`).
* `saveAudit(const std::string& filename, const std::vector<std::string>& records)`: Writes a `uint32_t` record count and length-prefixed strings to `<filename>.tmp` in binary mode, flushes/closes, and atomically replaces `filename` (`MoveFileExA` on `_WIN32`, `std::rename` otherwise).
* `loadAudit(const std::string& filename)`: Reads and validates the binary audit file (rejecting `count > 10000` or record `size > 1000000`), returning `{}` if the file cannot be opened.

---

## Interactive Menu & Runtime Workflow (`main`)

On startup, `main()` seeds `DistributionDepot` with `RationKit`, `TraumaMedicine`, `CleanWater`, and `ThermalBlanket`, and attempts to load `relief_audit.bin`. It then presents an interactive loop (`0`–`11`) guarded by `readInt` and `readDouble`:

| Option | Action | Behavior & Side Effects |
| :--- | :--- | :--- |
| **1** | View inventory | Prints supply name, quantity, kg/unit, and $\text{m}^3$/unit. |
| **2** | Restock supplies | Reads `name quantity unitWeight unitVolume`, applies `depot += cargo`, and saves a `RESTOCK` audit entry. |
| **3** | Allocate relief supplies | Runs either volume-based (`method == 1`) or medical-priority (`method == 2`) `allocate(...)`; queues non-zero allocations in `pendingCargo` and logs `ALLOCATED`. |
| **4** | Emergency triage override | Calls `IncidentCommander::criticalTriageOverride(...)`, queues non-zero cargo in `pendingCargo`, and logs `TRIAGE OVERRIDE`. |
| **5** | Register transport vehicle | Creates a `GroundConvoy`, `AerialTransport`, `AmphibiousCraft`, or `HybridMedicalUnit` on the heap, adds it to `fleet`, and logs `VEHICLE REGISTERED`. |
| **6** | View fleet and manifests | Demonstrates `TransportVehicle` copy construction, `operator<<` / `operator>>` round-trip serialization, `operator==`, `manifestTable`, and `hasDuplicateCargo()`. |
| **7** | Load allocated cargo | Moves a selected item from `pendingCargo` into a chosen vehicle's manifest via `vehicle->load(...)` and logs `LOADED`. |
| **8** | Dispatch vehicle | Checks non-empty manifest and prevents duplicate dispatch by ID; collects fuel, corridor damage status, destination zone, and `(x, y)` coordinates (computing distance from `(0, 0)`), runs `evaluateRoute()`, updates `dispatchedTonnes()`, and logs `DISPATCHED` (or catches route exceptions). |
| **9** | Manage urgent medical needs | Collects medical needs with priorities `1–10`, sorts with `UrgencyFirst`, and displays the next item from `PriorityDispatchQueue<MedicalNeed, UrgencyFirst>`. |
| **10** | View audit logs | Prints all loaded/recorded audit entries. |
| **11** | View logistics statistics | Displays registered fleet size, `TransportVehicle::activeVehicles()`, dispatched count, `TransportVehicle::dispatchedTonnes()`, and pending cargo batch count. |
| **0** | Exit | Deletes all heap-allocated `ReliefVehicle*` instances in `fleet` (triggering virtual destructors) and prints the final active vehicle count. |

---

## Build & Usage Notes
* **Standard & Platform**: Requires a C++17 (or later) compiler (uses inline static members and nested namespaces); includes `<windows.h>` with `NOMINMAX` when `_WIN32` is defined.
* **Input Constraints**: Several prompts expect single-token inputs without spaces (e.g., supply names, vehicle IDs, destination zones, and medical need names).
* **Persistence**: Audit logs are written to `relief_audit.bin` in the current working directory.