// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/test/AgentHwTest.h"

#include "fboss/agent/test/gen-cpp2/production_features_types.h"

DECLARE_bool(intf_nbr_tables);
namespace facebook::fboss {

class AgentL3ForwardingTest : public AgentHwTest {
 public:
  std::vector<production_features::ProductionFeature>
  getProductionFeaturesVerified() const override {
    return {};
  }

 protected:
  std::optional<VlanID> kVlanID() const {
    return utility::firstVlanID(getProgrammedState());
  }
  InterfaceID kIntfID() const {
    return utility::firstInterfaceID(getProgrammedState());
  }

  folly::MacAddress kNeighborMac() const {
    // Random mac address
    return folly::MacAddress("0c:42:a1:66:1d:9e");
  }
  template <typename IPAddrT>
  auto getNeighborTable(const std::shared_ptr<SwitchState>& in) {
    auto state = in;
    ;
    if (FLAGS_intf_nbr_tables) {
      return state->getInterfaces()
          ->getNode(kIntfID())
          ->template getNeighborEntryTable<IPAddrT>()
          ->modify(kIntfID(), &state);
    } else {
      return state->getVlans()
          ->getNode(*kVlanID())
          ->template getNeighborEntryTable<IPAddrT>()
          ->modify(*kVlanID(), &state);
    }
  }
  template <typename IPAddrT>
  std::shared_ptr<SwitchState> addResolvedNeighbor(
      const std::shared_ptr<SwitchState>& in,
      const IPAddrT& ip) {
    auto outState{in->clone()};
    auto neighborTable = getNeighborTable<IPAddrT>(outState);
    neighborTable->addPendingEntry(ip, kIntfID());
    auto ports = getPortsForInterface(kIntfID(), outState);
    CHECK(ports.size());
    neighborTable->updateEntry(
        ip,
        kNeighborMac(),
        PortDescriptor(*ports.begin()),
        kIntfID(),
        NeighborState::REACHABLE);
    return outState;
  }
  template <typename IPAddrT>
  std::shared_ptr<SwitchState> removeNeighbor(
      const std::shared_ptr<SwitchState>& inState,
      const IPAddrT& ip) {
    auto outState{inState->clone()};
    auto neighborTable = getNeighborTable<IPAddrT>(outState);
    neighborTable->removeEntry(ip);
    return outState;
  }
};

TEST_F(AgentL3ForwardingTest, linkLocalNextHop) {
  auto setup = [=]() {
    // Random LL IP
    folly::IPAddress linkLocalNhop("fe80::e42:a1ff:fe66:1d9e");
    getSw()->updateStateBlocking(
        "add nbr", [&](const std::shared_ptr<SwitchState>& in) {
          return addResolvedNeighbor(in, linkLocalNhop.asV6());
        });
    RouteNextHopSet nhops;
    nhops.emplace(ResolvedNextHop(linkLocalNhop, kIntfID(), ECMP_WEIGHT));
    auto routeUpdater = getSw()->getRouteUpdater();
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2803:1::"),
        64,
        ClientID::BGPD,
        RouteNextHopEntry(nhops, AdminDistance::EBGP));
    routeUpdater.program();
  };
  verifyAcrossWarmBoots(setup, []() {});
}
} // namespace facebook::fboss