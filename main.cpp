#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <functional>
#include <limits>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace ReliefOps { namespace Logistics { class IncidentCommander; } }

namespace ReliefOps::Inventory {

class ReliefCargo {
    std::string name_;
    int quantity_ = 0;
    double unitWeight_ = 0.0, unitVolume_ = 0.0;

    friend class DistributionDepot;
    friend class ReliefOps::Logistics::IncidentCommander;

public:
    ReliefCargo(std::string name = "", int q = 0, double weight = 0, double volume = 0)
        : name_(std::move(name)), quantity_(q), unitWeight_(weight), unitVolume_(volume) {}

    const std::string& name() const { return name_; }
    int quantity() const { return quantity_; }
    double unitWeight() const { return unitWeight_; }
    double unitVolume() const { return unitVolume_; }
    void setQuantity(int q) { quantity_ = q; }

    bool operator==(const ReliefCargo& other) const { return name_ == other.name_; }

    friend std::ostream& operator<<(std::ostream& out, const ReliefCargo& c) {
        return out << c.name_ << ' ' << c.quantity_ << ' '
                   << c.unitWeight_ << ' ' << c.unitVolume_;
    }
    friend std::istream& operator>>(std::istream& in, ReliefCargo& c) {
        return in >> c.name_ >> c.quantity_ >> c.unitWeight_ >> c.unitVolume_;
    }
};

class DistributionDepot {
    std::vector<ReliefCargo> stock_;
    friend class ReliefOps::Logistics::IncidentCommander;

public:
    // Return-by-reference lets an allocation routine update stock directly.
    int& stockCount(const std::string& name) {
        for (auto& item : stock_)
            if (item.name_ == name) return item.quantity_;
        throw std::out_of_range("Supply not found in depot");
    }

    const std::vector<ReliefCargo>& items() const { return stock_; }

    DistributionDepot& operator+=(const ReliefCargo& cargo) {
        for (auto& item : stock_) {
            if (item == cargo) { item.quantity_ += cargo.quantity_; return *this; }
        }
        stock_.push_back(cargo);
        return *this;
    }

    DistributionDepot& operator-=(const ReliefCargo& cargo) {
        int& available = stockCount(cargo.name());
        if (available < cargo.quantity()) throw std::runtime_error("Insufficient depot stock");
        available -= cargo.quantity();
        return *this;
    }
};

} // namespace ReliefOps::Inventory

namespace ReliefOps::Logistics {

using Inventory::ReliefCargo;
using Inventory::DistributionDepot;

class CargoOverweightException : public std::exception {
public: const char* what() const noexcept override { return "Cargo exceeds vehicle payload capacity"; }
};
class FuelDepletionException : public std::exception {
public: const char* what() const noexcept override { return "Vehicle has insufficient fuel"; }
};
class DamagedCorridorException : public std::exception {
public: const char* what() const noexcept override { return "Planned route is damaged"; }
};

inline double distanceKm(double x1, double y1, double x2, double y2) {
    return std::sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
}
inline double payloadKg(const ReliefCargo& c) {
    return c.quantity() * c.unitWeight();
}

// Overload 1: allocate according to available cargo volume.
ReliefCargo& allocate(DistributionDepot& depot, ReliefCargo& mission, double& freeVolume) {
    int& stock = depot.stockCount(mission.name());
    int byVolume = (mission.unitVolume() > 0 && freeVolume > 0)
        ? static_cast<int>(freeVolume / mission.unitVolume()) : 0;
    int sent = std::min({mission.quantity(), stock, byVolume});
    stock -= sent;
    mission.setQuantity(sent);
    freeVolume -= sent * mission.unitVolume();
    return mission;
}

// Overload 2: urgent medical allocation; patients is updated by reference.
ReliefCargo& allocate(DistributionDepot& depot, ReliefCargo& mission,
                      int& patients, int priority) {
    int& stock = depot.stockCount(mission.name());
    int request = (priority >= 8) ? patients : (patients + 1) / 2;
    int sent = std::min(request, stock);
    stock -= sent;
    patients -= sent;
    mission.setQuantity(sent);
    return mission;
}

class IncidentCommander {
public:
    // Friend access permits a critical triage override of normal allocation.
    static ReliefCargo& criticalTriageOverride(DistributionDepot& depot,
                                                ReliefCargo& mission, int units) {
        for (auto& item : depot.stock_) {
            if (item.name_ == mission.name_) {
                int sent = std::min(units, item.quantity_);
                item.quantity_ -= sent;
                mission.quantity_ = sent;
                return mission;
            }
        }
        throw std::out_of_range("Emergency supply not found");
    }
};

class TransportVehicle {
protected:
    ReliefCargo* manifest_;
    std::size_t slots_, used_ = 0;
    inline static int activeVehicles_ = 0;
    inline static double dispatchedTonnes_ = 0.0;

public:
    explicit TransportVehicle(std::size_t slots = 0)
        : manifest_(slots ? new ReliefCargo[slots] : nullptr), slots_(slots) {
        ++activeVehicles_;
    }
    // Deep-copy constructor for the dynamically allocated manifest.
    TransportVehicle(const TransportVehicle& other)
        : manifest_(other.slots_ ? new ReliefCargo[other.slots_] : nullptr),
          slots_(other.slots_), used_(other.used_) {
        for (std::size_t i = 0; i < used_; ++i) manifest_[i] = other.manifest_[i];
        ++activeVehicles_;
    }
    TransportVehicle& operator=(const TransportVehicle&) = delete;
    virtual ~TransportVehicle() { delete[] manifest_; --activeVehicles_; }

    bool load(const ReliefCargo& cargo) {
        if (used_ == slots_) return false;
        manifest_[used_++] = cargo;
        return true;
    }
    const ReliefCargo& item(std::size_t i) const { return manifest_[i]; }
    std::size_t itemCount() const { return used_; }

    double manifestWeightKg() const {
        double total = 0;
        for (std::size_t i = 0; i < used_; ++i) total += payloadKg(manifest_[i]);
        return total;
    }
    bool hasDuplicateCargo() const {
        for (std::size_t i = 0; i < used_; ++i)
            for (std::size_t j = i + 1; j < used_; ++j)
                if (manifest_[i] == manifest_[j]) return true;
        return false;
    }
    bool operator==(const TransportVehicle& other) const {
        if (used_ != other.used_) return false;
        for (std::size_t i = 0; i < used_; ++i)
            if (!(manifest_[i] == other.manifest_[i])) return false;
        return true;
    }
    void dispatch(double tonnes) { dispatchedTonnes_ += tonnes; }
    static int activeVehicles() { return activeVehicles_; }
    static double dispatchedTonnes() { return dispatchedTonnes_; }

    // The count followed by cargo records is also the manifest serialization format.
    friend std::ostream& operator<<(std::ostream& out, const TransportVehicle& v) {
        out << v.used_ << '\n';
        for (std::size_t i = 0; i < v.used_; ++i) out << v.manifest_[i] << '\n';
        return out;
    }
    friend std::istream& operator>>(std::istream& in, TransportVehicle& v) {
        std::size_t count;
        if (!(in >> count)) return in;
        v.used_ = 0;
        for (std::size_t i = 0; i < count; ++i) {
            ReliefCargo c;
            if (!(in >> c)) return in;
            if (v.used_ < v.slots_) v.manifest_[v.used_++] = c;
        }
        return in;
    }
};

class GPSBeacon {
    std::string zone_ = "Unassigned";
public:
    void setZone(const std::string& z) { zone_ = z; }
    const std::string& zone() const { return zone_; }
};
class EmergencyLifeSupportUnit {
    int reserveMinutes_ = 180;
public:
    bool ready() const { return reserveMinutes_ > 0; }
};

class ReliefVehicle : public TransportVehicle {
    std::string id_;
    double capacityKg_, fuel_ = 100;
    bool damagedCorridor_ = false;
    GPSBeacon gps_;                              // composition
    EmergencyLifeSupportUnit lifeSupport_;       // composition
protected:
    void checkRoute() const {
        if (manifestWeightKg() > capacityKg_) throw CargoOverweightException();
        if (fuel_ <= 0) throw FuelDepletionException();
        if (damagedCorridor_) throw DamagedCorridorException();
    }
public:
    ReliefVehicle(double cap, std::size_t slots, std::string id)
        : TransportVehicle(slots), id_(std::move(id)), capacityKg_(cap) {}
    virtual void evaluateRoute() const = 0;
    virtual std::string type() const = 0;
    const std::string& destinationZone() const { return gps_.zone(); }
    bool lifeSupportReady() const { return lifeSupport_.ready(); }
    double capacityKg() const { return capacityKg_; }
    double manifestWeight() const { return manifestWeightKg(); }
    virtual ~ReliefVehicle() = default;
    void setFuel(double f) { fuel_ = f; }
    void setDamagedCorridor(bool b) { damagedCorridor_ = b; }
    void setZone(const std::string& z) { gps_.setZone(z); }
    const std::string& id() const { return id_; }
};

class AerialTransport : virtual public ReliefVehicle {
public:
    AerialTransport(double cap=500, std::size_t slots=4, std::string id="HELI-1")
        : ReliefVehicle(cap, slots, std::move(id)) {}
    void evaluateRoute() const override { checkRoute(); std::cout << id() << ": air corridor selected to " << destinationZone()
                  << " | life support: " << (lifeSupportReady() ? "ready" : "not ready") << '\n'; }
    std::string type() const override { return "Aerial transport"; }
    ~AerialTransport() override { std::cout << "Aerial transport deallocated\n"; }
};
class GroundConvoy : virtual public ReliefVehicle {
public:
    GroundConvoy(double cap=2000, std::size_t slots=4, std::string id="TRUCK-1")
        : ReliefVehicle(cap, slots, std::move(id)) {}
    void evaluateRoute() const override { checkRoute(); std::cout << id() << ": safest road route selected to " << destinationZone()
                  << " | life support: " << (lifeSupportReady() ? "ready" : "not ready") << '\n'; }
    std::string type() const override { return "Ground convoy"; }
    ~GroundConvoy() override { std::cout << "Ground convoy deallocated\n"; }
};
class AmphibiousCraft : virtual public ReliefVehicle {
public:
    AmphibiousCraft(double cap=3000, std::size_t slots=4, std::string id="BOAT-1")
        : ReliefVehicle(cap, slots, std::move(id)) {}
    void evaluateRoute() const override { checkRoute(); std::cout << id() << ": water route selected to " << destinationZone()
                  << " | life support: " << (lifeSupportReady() ? "ready" : "not ready") << '\n'; }
    std::string type() const override { return "Amphibious craft"; }
    ~AmphibiousCraft() override { std::cout << "Amphibious craft deallocated\n"; }
};

// Both parents virtually inherit ReliefVehicle, so the hybrid has one base instance.
class HybridMedicalUnit : public GroundConvoy, public AmphibiousCraft {
public:
    HybridMedicalUnit(double cap=2500, std::size_t slots=4, std::string id="HYBRID-1")
        : ReliefVehicle(cap, slots, std::move(id)), GroundConvoy(), AmphibiousCraft() {}
    void evaluateRoute() const override { checkRoute(); std::cout << id() << ": land/water medical route selected to " << destinationZone()
                  << " | life support: " << (lifeSupportReady() ? "ready" : "not ready") << '\n'; }
    std::string type() const override { return "Hybrid medical unit"; }
    ~HybridMedicalUnit() override { std::cout << "Hybrid medical unit deallocated\n"; }
};

struct MedicalNeed {
    std::string item;
    int priority;
    bool operator<(const MedicalNeed& other) const { return priority < other.priority; }
};
struct UrgencyFirst {
    bool operator()(const MedicalNeed& a, const MedicalNeed& b) const {
        return a.priority > b.priority;
    }
};

template<class T, class Compare = std::less<T>>
class PriorityDispatchQueue {
    std::priority_queue<T, std::vector<T>, Compare> queue_;
public:
    void push(const T& item) { queue_.push(item); }
    void pop() { queue_.pop(); }
    const T& top() const { return queue_.top(); }
    bool empty() const { return queue_.empty(); }
};

// User-defined stream manipulator for a tabular manifest heading.
std::ostream& manifestTable(std::ostream& out) {
    return out << std::left << std::setw(18) << "SUPPLY"
               << std::setw(10) << "QUANTITY" << "KG/UNIT\n";
}

// Binary audit records are written to a temporary file and renamed into place.
void saveAudit(const std::string& filename, const std::vector<std::string>& records) {
    const std::string temp = filename + ".tmp";
    std::fstream out(temp, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot create temporary audit file");
    std::uint32_t count = static_cast<std::uint32_t>(records.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& record : records) {
        std::uint32_t size = static_cast<std::uint32_t>(record.size());
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        out.write(record.data(), size);
    }
    out.flush();
    if (!out) throw std::runtime_error("Audit write failed");
    out.close();
    if (!out) throw std::runtime_error("Audit file close failed");
#ifdef _WIN32
    if (!MoveFileExA(temp.c_str(), filename.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::remove(temp.c_str());
        throw std::runtime_error("Atomic audit-file replacement failed");
    }
#else
    if (std::rename(temp.c_str(), filename.c_str()) != 0) {
        std::remove(temp.c_str());
        throw std::runtime_error("Atomic audit-file rename failed");
    }
#endif
}

std::vector<std::string> loadAudit(const std::string& filename) {
    std::fstream in(filename, std::ios::in | std::ios::binary);
    if (!in) return {};
    std::uint32_t count;
    if (!in.read(reinterpret_cast<char*>(&count), sizeof(count)))
        throw std::runtime_error("Invalid audit file");
    if (count > 10000) throw std::runtime_error("Audit file is too large");
    std::vector<std::string> records;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t size;
        if (!in.read(reinterpret_cast<char*>(&size), sizeof(size)) || size > 1000000)
            throw std::runtime_error("Invalid audit record");
        std::string record(size, '\0');
        if (size && !in.read(record.data(), size)) throw std::runtime_error("Truncated audit record");
        records.push_back(record);
    }
    return records;
}

} // namespace ReliefOps::Logistics

// Small input helpers keep the menu safe from invalid numeric input.
int readInt(const std::string& prompt, int minValue, int maxValue) {
    int value;
    while (true) {
        std::cout << prompt;
        if (std::cin >> value && value >= minValue && value <= maxValue) return value;
        std::cout << "Invalid input. Enter a value from " << minValue
                  << " to " << maxValue << ".\n";
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

double readDouble(const std::string& prompt, double minValue, double maxValue) {
    double value;
    while (true) {
        std::cout << prompt;
        if (std::cin >> value && value >= minValue && value <= maxValue) return value;
        std::cout << "Invalid input. Enter a value from " << minValue
                  << " to " << maxValue << ".\n";
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

int main() {
    using namespace ReliefOps::Inventory;
    using namespace ReliefOps::Logistics;

    DistributionDepot depot;
    depot += ReliefCargo("RationKit", 500, 1.0, 0.002);
    depot += ReliefCargo("TraumaMedicine", 100, 0.2, 0.001);
    depot += ReliefCargo("CleanWater", 50, 1000, 1.0);
    depot += ReliefCargo("ThermalBlanket", 200, 0.8, 0.01);

    // Base pointers demonstrate runtime polymorphism and virtual destruction.
    std::vector<ReliefVehicle*> fleet;
    std::vector<ReliefCargo> pendingCargo;
    std::vector<std::string> audit;
    std::vector<std::string> dispatchedIDs;
    std::string auditFile = "relief_audit.bin";

    try {
        audit = loadAudit(auditFile);
        std::cout << "Loaded " << audit.size() << " previous audit record(s).\n";
    } catch (const std::exception& e) {
        std::cout << "Could not read old audit file: " << e.what() << "\n";
    }

    auto saveRecord = [&](const std::string& record) {
        audit.push_back(record);
        try {
            saveAudit(auditFile, audit);
            std::cout << "Audit record saved.\n";
        } catch (const std::exception& e) {
            std::cout << "Audit save failed: " << e.what() << "\n";
        }
    };

    auto showInventory = [&]() {
        std::unordered_map<std::string, int> stock;
        for (const auto& item : depot.items()) stock[item.name()] = item.quantity();
        std::cout << "\n--- AVAILABLE INVENTORY ---\n";
        std::cout << std::left << std::setw(20) << "SUPPLY" << std::setw(12)
                  << "QUANTITY" << std::setw(14) << "KG/UNIT" << "M3/UNIT\n";
        for (const auto& item : depot.items()) {
            std::cout << std::left << std::setw(20) << item.name()
                      << std::setw(12) << stock[item.name()]
                      << std::setw(14) << item.unitWeight()
                      << item.unitVolume() << '\n';
        }
    };

    auto chooseSupply = [&]() -> const ReliefCargo* {
        if (depot.items().empty()) {
            std::cout << "No supplies in the depot.\n";
            return nullptr;
        }
        std::cout << "\nSelect supply:\n";
        for (std::size_t i = 0; i < depot.items().size(); ++i)
            std::cout << i + 1 << ". " << depot.items()[i].name()
                      << " (stock: " << depot.items()[i].quantity() << ")\n";
        int choice = readInt("Choice: ", 1, static_cast<int>(depot.items().size()));
        return &depot.items()[choice - 1];
    };

    auto chooseVehicle = [&]() -> ReliefVehicle* {
        if (fleet.empty()) {
            std::cout << "No vehicles registered. Register a vehicle first.\n";
            return nullptr;
        }
        std::cout << "\nSelect vehicle:\n";
        for (std::size_t i = 0; i < fleet.size(); ++i)
            std::cout << i + 1 << ". " << fleet[i]->id() << " - "
                      << fleet[i]->type() << "\n";
        int choice = readInt("Choice: ", 1, static_cast<int>(fleet.size()));
        return fleet[choice - 1];
    };

    int choice;
    do {
        std::cout << "\n========== ERL-HUB =========="
                  << "\n1. View inventory"
                  << "\n2. Restock supplies"
                  << "\n3. Allocate relief supplies"
                  << "\n4. Emergency triage override"
                  << "\n5. Register transport vehicle"
                  << "\n6. View fleet and manifests"
                  << "\n7. Load allocated cargo"
                  << "\n8. Dispatch vehicle"
                  << "\n9. Manage urgent medical needs"
                  << "\n10. View audit logs"
                  << "\n11. View logistics statistics"
                  << "\n0. Exit\n";
        choice = readInt("Enter your choice: ", 0, 11);

        try {
            switch (choice) {
            case 1:
                showInventory();
                break;

            case 2: {
                ReliefCargo cargo;
                std::cout << "Enter: SupplyName Quantity UnitWeightKg UnitVolumeM3\n";
                std::cout << "Example: RationKit 100 1.0 0.002\n> ";
                if (std::cin >> cargo && cargo.quantity() >= 0 &&
                    cargo.unitWeight() >= 0 && cargo.unitVolume() >= 0) {
                    depot += cargo; // overloaded += restocks the depot
                    saveRecord("RESTOCK: " + cargo.name() + " x" +
                               std::to_string(cargo.quantity()));
                } else {
                    std::cout << "Invalid cargo details.\n";
                    std::cin.clear();
                    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                }
                break;
            }

            case 3: {
                const ReliefCargo* supply = chooseSupply();
                if (!supply) break;
                int method = readInt("1. Volume-based allocation  2. Medical-priority allocation: ", 1, 2);
                ReliefCargo mission(supply->name(), 0, supply->unitWeight(), supply->unitVolume());
                if (method == 1) {
                    int requested = readInt("Units requested: ", 1, 1000000);
                    double freeVolume = readDouble("Available vehicle volume (m3): ", 0.000001, 1000000);
                    mission.setQuantity(requested);
                    ReliefCargo& allocated = allocate(depot, mission, freeVolume);
                    std::cout << "Allocated " << allocated.quantity()
                              << " unit(s). Remaining free volume: " << freeVolume << " m3\n";
                } else {
                    int patients = readInt("Patients requiring treatment: ", 1, 1000000);
                    int priority = readInt("Priority (1-10): ", 1, 10);
                    ReliefCargo& allocated = allocate(depot, mission, patients, priority);
                    std::cout << "Allocated " << allocated.quantity()
                              << " unit(s). Patients still waiting: " << patients << '\n';
                }
                if (mission.quantity() > 0) {
                    pendingCargo.push_back(mission);
                    saveRecord("ALLOCATED: " + mission.name() + " x" +
                               std::to_string(mission.quantity()));
                    std::cout << "Cargo added to pending manifest list.\n";
                } else {
                    std::cout << "No cargo was allocated.\n";
                }
                break;
            }

            case 4: {
                const ReliefCargo* supply = chooseSupply();
                if (!supply) break;
                int units = readInt("Emergency units requested: ", 1, 1000000);
                ReliefCargo emergency(supply->name(), 0, supply->unitWeight(), supply->unitVolume());
                IncidentCommander::criticalTriageOverride(depot, emergency, units);
                std::cout << "Commander override allocated " << emergency.quantity()
                          << " unit(s).\n";
                if (emergency.quantity() > 0) {
                    pendingCargo.push_back(emergency);
                    saveRecord("TRIAGE OVERRIDE: " + emergency.name() + " x" +
                               std::to_string(emergency.quantity()));
                }
                break;
            }

            case 5: {
                int type = readInt("1. Ground truck  2. Aerial vehicle (helicopter/drone)  3. Amphibious boat  4. Hybrid medical unit: ", 1, 4);
                std::string id;
                std::cout << "Vehicle ID (no spaces): ";
                std::cin >> id;
                double capacity = readDouble("Payload capacity (kg): ", 1, 1000000);
                int slots = readInt("Manifest item slots: ", 1, 100);
                ReliefVehicle* vehicle = nullptr;
                if (type == 1) vehicle = new GroundConvoy(capacity, slots, id);
                if (type == 2) vehicle = new AerialTransport(capacity, slots, id);
                if (type == 3) vehicle = new AmphibiousCraft(capacity, slots, id);
                if (type == 4) vehicle = new HybridMedicalUnit(capacity, slots, id);
                fleet.push_back(vehicle);
                std::cout << vehicle->type() << " registered successfully.\n";
                saveRecord("VEHICLE REGISTERED: " + id + " (" + vehicle->type() + ")");
                break;
            }

            case 6: {
                if (fleet.empty()) {
                    std::cout << "No vehicles registered.\n";
                    break;
                }
                for (ReliefVehicle* vehicle : fleet) {
                    std::cout << "\n" << vehicle->id() << " - " << vehicle->type()
                              << " | Load: " << vehicle->manifestWeight()
                              << " / " << vehicle->capacityKg() << " kg\n";
                    // Copy constructor, ==, << and >> are demonstrated here.
                    TransportVehicle copied(*vehicle);
                    std::stringstream serialized;
                    serialized << copied;
                    TransportVehicle restored(vehicle->itemCount());
                    serialized >> restored;
                    std::cout << "Copied manifest matches: "
                              << (copied == *vehicle ? "yes" : "no") << '\n';
                    std::cout << manifestTable;
                    for (std::size_t i = 0; i < restored.itemCount(); ++i) {
                        const ReliefCargo& cargo = restored.item(i);
                        std::cout << std::left << std::setw(18) << cargo.name()
                                  << std::setw(10) << cargo.quantity()
                                  << cargo.unitWeight() << '\n';
                    }
                    std::cout << "Duplicate cargo identity: "
                              << (vehicle->hasDuplicateCargo() ? "yes" : "no") << '\n';
                }
                break;
            }

            case 7: {
                if (pendingCargo.empty()) {
                    std::cout << "No allocated cargo is waiting to be loaded.\n";
                    break;
                }
                std::cout << "\nPending cargo:\n";
                for (std::size_t i = 0; i < pendingCargo.size(); ++i)
                    std::cout << i + 1 << ". " << pendingCargo[i].name()
                              << " x" << pendingCargo[i].quantity() << '\n';
                int cargoChoice = readInt("Select cargo: ", 1, static_cast<int>(pendingCargo.size()));
                ReliefVehicle* vehicle = chooseVehicle();
                if (!vehicle) break;
                ReliefCargo cargo = pendingCargo[cargoChoice - 1];
                if (vehicle->load(cargo)) {
                    pendingCargo.erase(pendingCargo.begin() + cargoChoice - 1);
                    std::cout << cargo.name() << " loaded into " << vehicle->id() << ".\n";
                    saveRecord("LOADED: " + cargo.name() + " into " + vehicle->id());
                } else {
                    std::cout << "Vehicle manifest is full.\n";
                }
                break;
            }

            case 8: {
                ReliefVehicle* vehicle = chooseVehicle();
                if (!vehicle) break;
                if (vehicle->itemCount() == 0) {
                    std::cout << "Cannot dispatch an empty vehicle.\n";
                    break;
                }
                if (std::find(dispatchedIDs.begin(), dispatchedIDs.end(), vehicle->id()) != dispatchedIDs.end()) {
                    std::cout << "This vehicle has already been dispatched.\n";
                    break;
                }
                double fuel = readDouble("Fuel level (0-100): ", 0, 100);
                int damaged = readInt("Is the planned corridor damaged? (1=yes, 0=no): ", 0, 1);
                std::string zone;
                std::cout << "Destination zone (one word): ";
                std::cin >> zone;
                vehicle->setFuel(fuel);
                vehicle->setDamagedCorridor(damaged == 1);
                vehicle->setZone(zone);
                double x = readDouble("Destination map X coordinate: ", -100000, 100000);
                double y = readDouble("Destination map Y coordinate: ", -100000, 100000);
                std::cout << "Approx. route distance: " << distanceKm(0, 0, x, y) << " km\n";
                try {
                    vehicle->evaluateRoute();
                    double tonnes = vehicle->manifestWeight() / 1000.0;
                    vehicle->dispatch(tonnes);
                    dispatchedIDs.push_back(vehicle->id());
                    std::ostringstream record;
                    record << "DISPATCHED: " << vehicle->id() << " to " << zone
                           << ", " << tonnes << " metric tonnes";
                    saveRecord(record.str());
                    std::cout << "Dispatch successful.\n";
                } catch (const CargoOverweightException& e) {
                    std::cout << "Dispatch blocked: " << e.what() << '\n';
                } catch (const FuelDepletionException& e) {
                    std::cout << "Dispatch blocked: " << e.what() << '\n';
                } catch (const DamagedCorridorException& e) {
                    std::cout << "Dispatch blocked: " << e.what() << '\n';
                }
                break;
            }

            case 9: {
                int count = readInt("Number of urgent medical needs: ", 1, 50);
                std::vector<MedicalNeed> needs;
                PriorityDispatchQueue<MedicalNeed, UrgencyFirst> urgentQueue;
                for (int i = 0; i < count; ++i) {
                    MedicalNeed need;
                    std::cout << "Need name (no spaces): ";
                    std::cin >> need.item;
                    need.priority = readInt("Priority (1-10): ", 1, 10);
                    needs.push_back(need);
                    urgentQueue.push(need);
                }
                std::sort(needs.begin(), needs.end(), UrgencyFirst{});
                std::cout << "\nNeeds sorted by urgency:\n";
                for (const auto& need : needs)
                    std::cout << need.item << " - priority " << need.priority << '\n';
                if (!urgentQueue.empty())
                    std::cout << "Next priority dispatch: " << urgentQueue.top().item << '\n';
                break;
            }

            case 10:
                std::cout << "\n--- AUDIT LOGS ---\n";
                if (audit.empty()) std::cout << "No audit records available.\n";
                for (std::size_t i = 0; i < audit.size(); ++i)
                    std::cout << i + 1 << ". " << audit[i] << '\n';
                break;

            case 11:
                std::cout << "\n--- LOGISTICS STATISTICS ---\n"
                          << "Registered vehicles: " << fleet.size() << '\n'
                          << "Currently allocated vehicle objects: "
                          << TransportVehicle::activeVehicles() << '\n'
                          << "Successfully dispatched: " << dispatchedIDs.size() << '\n'
                          << "Total dispatched payload: "
                          << TransportVehicle::dispatchedTonnes() << " metric tonnes\n"
                          << "Pending cargo batches: " << pendingCargo.size() << '\n';
                break;

            case 0:
                std::cout << "Exiting ERL-HUB...\n";
                break;
            }
        } catch (const std::exception& e) {
            std::cout << "Operation failed: " << e.what() << '\n';
        }
    } while (choice != 0);

    // Delete through base pointers; virtual destructors call derived destructors.
    for (ReliefVehicle* vehicle : fleet) delete vehicle;
    std::cout << "All vehicles deallocated. Active count: "
              << TransportVehicle::activeVehicles() << '\n';
    return 0;
}
