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
    void evaluateRoute() const override { checkRoute(); std::cout << id() << ": air corridor selected\n"; }
    ~AerialTransport() override { std::cout << "Aerial transport deallocated\n"; }
};
class GroundConvoy : virtual public ReliefVehicle {
public:
    GroundConvoy(double cap=2000, std::size_t slots=4, std::string id="TRUCK-1")
        : ReliefVehicle(cap, slots, std::move(id)) {}
    void evaluateRoute() const override { checkRoute(); std::cout << id() << ": safest road route selected\n"; }
    ~GroundConvoy() override { std::cout << "Ground convoy deallocated\n"; }
};
class AmphibiousCraft : virtual public ReliefVehicle {
public:
    AmphibiousCraft(double cap=3000, std::size_t slots=4, std::string id="BOAT-1")
        : ReliefVehicle(cap, slots, std::move(id)) {}
    void evaluateRoute() const override { checkRoute(); std::cout << id() << ": water route selected\n"; }
    ~AmphibiousCraft() override { std::cout << "Amphibious craft deallocated\n"; }
};

// Both parents virtually inherit ReliefVehicle, so the hybrid has one base instance.
class HybridMedicalUnit : public GroundConvoy, public AmphibiousCraft {
public:
    HybridMedicalUnit()
        : ReliefVehicle(2500, 4, "HYBRID-1"), GroundConvoy(), AmphibiousCraft() {}
    void evaluateRoute() const override { checkRoute(); std::cout << id() << ": land/water medical route selected\n"; }
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

int main() {
    using namespace ReliefOps::Inventory;
    using namespace ReliefOps::Logistics;

    try {
        DistributionDepot depot;
        depot += ReliefCargo("RationKit", 500, 1.0, 0.002);
        depot += ReliefCargo("TraumaMedicine", 100, 0.2, 0.001);
        depot += ReliefCargo("CleanWater", 50, 1000, 1.0);
        depot += ReliefCargo("ThermalBlanket", 200, 0.8, 0.01);

        double freeVolume = 0.10;
        ReliefCargo rationMission("RationKit", 100, 1.0, 0.002);
        ReliefCargo& rations = allocate(depot, rationMission, freeVolume);

        int patients = 12;
        ReliefCargo medicalMission("TraumaMedicine", 0, 0.2, 0.001);
        ReliefCargo& medicine = allocate(depot, medicalMission, patients, 10);

        ReliefCargo emergency("TraumaMedicine", 0, 0.2, 0.001);
        IncidentCommander::criticalTriageOverride(depot, emergency, 3);

        depot += ReliefCargo("CleanWater", 5, 1000, 1.0); // restock using +=
        depot -= ReliefCargo("CleanWater", 1, 1000, 1.0); // dispatch using -=

        std::cout << "Rations allocated: " << rations.quantity() << '\n'
                  << "Medicine allocated: " << medicine.quantity()
                  << ", patients still waiting: " << patients << '\n'
                  << "Emergency override units: " << emergency.quantity() << '\n';

        std::vector<ReliefVehicle*> fleet;
        fleet.push_back(new GroundConvoy(2000, 4, "TRUCK-01"));
        fleet.push_back(new AerialTransport(500, 4, "HELI-01"));
        fleet.push_back(new AmphibiousCraft(3000, 4, "BOAT-01"));
        fleet.push_back(new HybridMedicalUnit());

        fleet[0]->load(rations);
        fleet[0]->load(medicine);
        fleet[0]->load(ReliefCargo("RationKit", 2, 1.0, 0.002)); // duplicate item identity
        fleet[0]->setDamagedCorridor(true);
        fleet[1]->load(medicine);
        fleet[1]->setFuel(0);
        fleet[2]->load(ReliefCargo("CleanWater", 2, 1000, 1.0));
        fleet[2]->setZone("Coastal Relief Zone");
        fleet[3]->load(medicine);

        fleet[0]->dispatch((payloadKg(rations) + payloadKg(medicine)) / 1000.0);
        std::cout << "Duplicate cargo in truck manifest: "
                  << (fleet[0]->hasDuplicateCargo() ? "yes" : "no") << '\n';

        for (ReliefVehicle* vehicle : fleet) {
            try { vehicle->evaluateRoute(); }
            catch (const std::exception& e) { std::cout << vehicle->id() << ": " << e.what() << '\n'; }
        }

        {
        // Deep-copy constructor and == manifest identity check.
        GroundConvoy copiedTruck(*dynamic_cast<GroundConvoy*>(fleet[0]));
        std::cout << "Copied manifest matches: " << (copiedTruck == *fleet[0]) << '\n';

        // Demonstrate >> manifest restoration from the << serialized format.
        std::stringstream serialized;
        serialized << *fleet[0];
        GroundConvoy restoredTruck(2000, 4, "RESTORED");
        serialized >> restoredTruck;

        std::cout << manifestTable;
        for (std::size_t i = 0; i < restoredTruck.itemCount(); ++i) {
            const auto& c = restoredTruck.item(i);
            std::cout << std::left << std::setw(18) << c.name()
                      << std::setw(10) << c.quantity() << c.unitWeight() << '\n';
        }

        std::vector<MedicalNeed> needs = {{"BurnKit", 5}, {"Blood", 10}, {"Oxygen", 8}};
        std::sort(needs.begin(), needs.end(), UrgencyFirst{});
        PriorityDispatchQueue<MedicalNeed> urgentQueue;
        for (const auto& need : needs) urgentQueue.push(need);
        std::cout << "Most urgent need: " << urgentQueue.top().item << '\n';

        std::unordered_map<std::string, int> stockView;
        stockView["RationKit"] = depot.stockCount("RationKit");
        std::cout << "Ration stock remaining: " << stockView["RationKit"] << '\n';
        std::cout << "Example distance: " << distanceKm(0, 0, 3, 4) << " km\n";

        std::ostringstream manifestLog;
        manifestLog << *fleet[0];
        saveAudit("relief_audit.bin", {"Post-earthquake dispatch", manifestLog.str()});
        std::cout << "Audit records restored: " << loadAudit("relief_audit.bin").size() << '\n';

        // Demonstrate overweight exception.
        AerialTransport overloaded(1, 2, "OVERLOAD-TEST");
        overloaded.load(rations);
        try { overloaded.evaluateRoute(); }
        catch (const CargoOverweightException& e) { std::cout << e.what() << '\n'; }

        }
        std::cout << "Active vehicles before fleet cleanup: "
                  << TransportVehicle::activeVehicles() << '\n'
                  << "Dispatched tonnes: " << TransportVehicle::dispatchedTonnes() << '\n';

        // Delete through base pointers: virtual destructors run correctly.
        for (ReliefVehicle* vehicle : fleet) delete vehicle;
        std::cout << "Active vehicles after fleet cleanup: "
                  << TransportVehicle::activeVehicles() << '\n';
    }
    catch (const std::exception& e) {
        std::cerr << "Relief hub error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
