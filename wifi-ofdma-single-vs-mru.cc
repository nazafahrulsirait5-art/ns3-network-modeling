// File: wifi-ofdma-single-vs-mru.cc
// Build:
//   ./ns3 configure -d release --enable-examples --enable-tests
//   ./ns3 build
// Run:
//   ./ns3 run 'wifi-ofdma-single-vs-mru --scenario=single --nSta=50'
//   ./ns3 run 'wifi-ofdma-single-vs-mru --scenario=mru --nSta=50'

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include <numeric>
#include <algorithm>
#include <iostream>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiOfdmaSingleVsMru");

static double JainFairness(const std::vector<double>& x)
{
  if (x.empty()) return 1.0;
  double sum = std::accumulate(x.begin(), x.end(), 0.0);
  double sumsq = 0.0;
  for (auto v : x) sumsq += v * v;
  if (sumsq <= 0.0) return 1.0;
  return (sum * sum) / (x.size() * sumsq);
}

int main(int argc, char* argv[])
{
  std::string scenario = "single"; // "single" | "mru"
  uint32_t nSta = 50;
  double simTime = 20.0;     // s
  double appStart = 1.0;     // s
  double offeredMbpsDl = 8;  // per-STA DL
  double offeredMbpsUl = 2;  // per-STA UL
  uint32_t pktSizeBytes = 1200;
  bool verbose = false;

  CommandLine cmd;
  cmd.AddValue("scenario", "single (20MHz) or mru (80MHz approx)", scenario);
  cmd.AddValue("nSta", "Number of stations", nSta);
  cmd.AddValue("simTime", "Simulation time (s)", simTime);
  cmd.AddValue("offeredMbpsDl", "Per-STA offered DL rate (Mbps)", offeredMbpsDl);
  cmd.AddValue("offeredMbpsUl", "Per-STA offered UL rate (Mbps)", offeredMbpsUl);
  cmd.AddValue("pktSizeBytes", "Application packet size (bytes)", pktSizeBytes);
  cmd.AddValue("verbose", "Enable verbose logging", verbose);
  cmd.Parse(argc, argv);

  if (verbose)
  {
    LogComponentEnable("WifiOfdmaSingleVsMru", LOG_LEVEL_INFO);
  }

  NodeContainer apNode;
  apNode.Create(1);
  NodeContainer staNodes;
  staNodes.Create(nSta);

  // Channel + PHY
  YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
  YansWifiPhyHelper phy;
  phy.SetChannel(channel.Create());

  // Atur kanal via ChannelSettings (hindari set ChannelWidth langsung)
  // Format: {channelNumber, channelWidthMHz, BAND_*, primary20Index}
  if (scenario == "single")
  {
    phy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));
  }
  else
  {
    phy.Set("ChannelSettings", StringValue("{42, 80, BAND_5GHZ, 0}"));
  }

  // Wi‑Fi HE (802.11ax)
  WifiHelper wifi;
  WifiMacHelper mac;
  wifi.SetStandard(WIFI_STANDARD_80211ax);
  wifi.SetRemoteStationManager("ns3::IdealWifiManager");

  Ssid ssid = Ssid("hdwlan");

  // AP
  mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
  NetDeviceContainer apDevs = wifi.Install(phy, mac, apNode);

  // STAs
  mac.SetType("ns3::StaWifiMac",
              "Ssid", SsidValue(ssid),
              "ActiveProbing", BooleanValue(false));
  NetDeviceContainer staDevs = wifi.Install(phy, mac, staNodes);

  // Mobilitas
  MobilityHelper apMob;
  Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
  pos->Add(Vector(0.0, 0.0, 1.5));
  apMob.SetPositionAllocator(pos);
  apMob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  apMob.Install(apNode);

  MobilityHelper staMob;
  staMob.SetPositionAllocator("ns3::GridPositionAllocator",
                              "MinX", DoubleValue(1.0),
                              "MinY", DoubleValue(1.0),
                              "DeltaX", DoubleValue(1.0),
                              "DeltaY", DoubleValue(1.0),
                              "GridWidth", UintegerValue(10),
                              "LayoutType", StringValue("RowFirst"));
  staMob.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                          "Bounds", RectangleValue(Rectangle(-15, 15, -15, 15)));
  staMob.Install(staNodes);

  InternetStackHelper stack;
  stack.Install(apNode);
  stack.Install(staNodes);

  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer apIf = ipv4.Assign(apDevs);
  Ipv4InterfaceContainer staIf = ipv4.Assign(staDevs);

  // Aplikasi UDP
  uint16_t dlPort = 5001;
  uint16_t ulPort = 6001;

  ApplicationContainer apps;

  for (uint32_t i = 0; i < nSta; ++i)
  {
    UdpServerHelper srv(dlPort);
    apps.Add(srv.Install(staNodes.Get(i)));

    OnOffHelper onoff("ns3::UdpSocketFactory",
                      InetSocketAddress(staIf.GetAddress(i), dlPort));
    onoff.SetAttribute("DataRate",
                       DataRateValue(DataRate(std::to_string((uint64_t)offeredMbpsDl) + "Mbps")));
    onoff.SetAttribute("PacketSize", UintegerValue(pktSizeBytes));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    apps.Add(onoff.Install(apNode.Get(0)));
  }

  {
    UdpServerHelper srv(ulPort);
    apps.Add(srv.Install(apNode.Get(0)));
  }
  for (uint32_t i = 0; i < nSta; ++i)
  {
    OnOffHelper onoff("ns3::UdpSocketFactory",
                      InetSocketAddress(apIf.GetAddress(0), ulPort));
    onoff.SetAttribute("DataRate",
                       DataRateValue(DataRate(std::to_string((uint64_t)offeredMbpsUl) + "Mbps")));
    onoff.SetAttribute("PacketSize", UintegerValue(pktSizeBytes));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    apps.Add(onoff.Install(staNodes.Get(i)));
  }

  apps.Start(Seconds(appStart));
  apps.Stop(Seconds(simTime));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // FlowMonitor
  FlowMonitorHelper fmHelper;
  Ptr<FlowMonitor> monitor = fmHelper.InstallAll();

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();

  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(fmHelper.GetClassifier());

  std::vector<double> perStaThroughputMbps(nSta, 0.0);
  std::vector<double> delays;
  double aggThroughputMbps = 0.0;

  for (const auto& kv : stats)
  {
    const auto& s = kv.second;
    double duration = (s.timeLastRxPacket - s.timeFirstRxPacket).GetSeconds();
    if (duration <= 0) continue;

    double tputMbps = (s.rxBytes * 8.0) / duration / 1e6;
    aggThroughputMbps += tputMbps;

    if (s.rxPackets > 0)
    {
      double avgDelay = s.delaySum.GetSeconds() / s.rxPackets;
      uint32_t reps = std::min<uint32_t>(s.rxPackets, 50);
      for (uint32_t k = 0; k < reps; ++k) delays.push_back(avgDelay);
    }

    if (classifier)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
      for (uint32_t i = 0; i < nSta; ++i)
      {
        if (t.destinationAddress == staIf.GetAddress(i) ||
            t.sourceAddress == staIf.GetAddress(i))
        {
          perStaThroughputMbps[i] += tputMbps;
          break;
        }
      }
    }
  }

double fairness = JainFairness(perStaThroughputMbps);
  if (!perStaThroughputMbps.empty())
  {
    double sum = std::accumulate(perStaThroughputMbps.begin(), perStaThroughputMbps.end(), 0.0);
    double sumsq = 0.0;
    for (auto v : perStaThroughputMbps) sumsq += v * v;
    if (sumsq > 0) fairness = (sum * sum) / (perStaThroughputMbps.size() * sumsq);
  }

  double avgDelay = 0.0;
  double p95Delay = 0.0;
  if (!delays.empty())
  {
    avgDelay = std::accumulate(delays.begin(), delays.end(), 0.0) / delays.size();
    std::sort(delays.begin(), delays.end());
    size_t idx = static_cast<size_t>(std::ceil(0.95 * delays.size())) - 1;
    p95Delay = delays[std::min(idx, delays.size() - 1)];
  }

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Scenario: " << scenario << "\n";
  std::cout << "Standard: 802.11ax (HE)\n";
  std::cout << "ChannelSettings: " << (scenario=="single" ? "20 MHz ch36" : "80 MHz ch42") << "\n";
  std::cout << "Stations: " << nSta << "\n";
  std::cout << "Aggregate Throughput: " << aggThroughputMbps << " Mbps\n";
  std::cout << "Avg Delay: " << (avgDelay * 1e3) << " ms\n";
  std::cout << "P95 Delay: " << (p95Delay * 1e3) << " ms\n";
  std::cout << "Jain Fairness: " << fairness << "\n";

  Simulator::Destroy();
  return 0;
}
