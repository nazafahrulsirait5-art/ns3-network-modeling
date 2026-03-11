/*
 * File: wifi-ofdma.cc (Skenario B: RU/MRU Forced)
 * Tipe: Low Latency / Small Packet Traffic
 * Tujuan: Memaksa aktivasi OFDMA (RU/MRU)
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiOfdma");

static double JainFairness(const std::vector<double>& x) {
    if (x.empty()) return 1.0;
    double sum = std::accumulate(x.begin(), x.end(), 0.0);
    double sumsq = 0.0;
    for (double v : x) sumsq += v * v;
    if (sumsq <= 0.0) return 1.0;
    return (sum * sum) / (x.size() * sumsq);
}

int main(int argc, char* argv[])
{
    std::string tech = "be"; 
    uint32_t width = 80;     
    uint32_t nSta = 20; // 20 User
    double simTime = 10.0;
    
    // --- SETINGAN KHUSUS MEMANCING RU / MRU ---
    // Kita kurangi load total agar buffer tidak overload (biar scheduler sempat mikir bagi RU)
    double offeredMbpsDl = 50.0;  // 50 Mbps tapi paketnya kecil-kecil (banyak)
    double offeredMbpsUl = 10.0;
    
    // KUNCI UTAMA: Paket Kecil (256 bytes) -> Memicu OFDMA
    uint32_t pktSizeBytes = 256; 

    CommandLine cmd;
    cmd.AddValue("tech", "Teknologi: ax (5GHz) atau be (6GHz)", tech);
    cmd.AddValue("width", "Lebar Kanal: 40, 80, 160", width);
    cmd.AddValue("nSta", "Jumlah Station", nSta);
    cmd.Parse(argc, argv);

    std::string band;
    int chNumber = 0;

    if (tech == "ax") {
        band = "BAND_5GHZ";
        if (width == 40) chNumber = 38;
        else if (width == 80) chNumber = 42;
        else if (width == 160) chNumber = 50;
    } else {
        band = "BAND_6GHZ";
        if (width == 40) chNumber = 35;
        else if (width == 80) chNumber = 39; 
        else if (width == 160) chNumber = 47;
    }

    NodeContainer ap; ap.Create(1);
    NodeContainer stas; stas.Create(nSta);

    YansWifiChannelHelper chan = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy; 
    phy.SetChannel(chan.Create());
    phy.Set("TxPowerStart", DoubleValue(21.0)); 
    phy.Set("TxPowerEnd", DoubleValue(21.0));

    std::ostringstream cs;
    cs << "{" << chNumber << ", " << width << ", " << band << ", 0}";
    phy.Set("ChannelSettings", StringValue(cs.str()));

    WifiHelper wifi;
    std::string dataMode, ctrlMode, mcsDisplay;

    if (tech == "ax") {
        wifi.SetStandard(WIFI_STANDARD_80211ax);
        dataMode = "HeMcs11"; ctrlMode = "HeMcs0"; 
        mcsDisplay = "OFDMA RU Enabled (Small Packet)";
    } else {
        wifi.SetStandard(WIFI_STANDARD_80211be);
        dataMode = "EhtMcs13"; ctrlMode = "EhtMcs0"; 
        mcsDisplay = "OFDMA MRU Enabled (Small Packet)";
    }

    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue(dataMode),
                                 "ControlMode", StringValue(ctrlMode));

    WifiMacHelper mac;
    Ssid ssid("riset-mru");

    // Enable MU-MIMO/OFDMA features
    mac.SetType("ns3::ApWifiMac", 
                "Ssid", SsidValue(ssid),
                "EnableBeaconJitter", BooleanValue(true),
                "EnableMuMimo", BooleanValue(true)); // Support Multi User
    
    NetDeviceContainer apDevs = wifi.Install(phy, mac, ap);

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevs = wifi.Install(phy, mac, stas);

    // MOBILITY: DIAM & DEKAT (Ideal)
    MobilityHelper apMob;
    Ptr<ListPositionAllocator> apPos = CreateObject<ListPositionAllocator>();
    apPos->Add(Vector(0.0, 0.0, 3.0));
    apMob.SetPositionAllocator(apPos);
    apMob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    apMob.Install(ap);

    MobilityHelper staMob;
    staMob.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(0.5), "MinY", DoubleValue(0.5),
                                "DeltaX", DoubleValue(0.5), "DeltaY", DoubleValue(0.5),
                                "GridWidth", UintegerValue(5), "LayoutType", StringValue("RowFirst"));
    staMob.SetMobilityModel("ns3::ConstantPositionMobilityModel"); 
    staMob.Install(stas);

    InternetStackHelper stack;
    stack.Install(ap); stack.Install(stas);
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apIf = ipv4.Assign(apDevs);
    Ipv4InterfaceContainer staIf = ipv4.Assign(staDevs);

    ApplicationContainer apps;
    uint16_t portDl = 5000, portUl = 6000;
    apps.Add(UdpServerHelper(portUl).Install(ap.Get(0)));
    for(uint32_t i=0; i<nSta; ++i) apps.Add(UdpServerHelper(portDl).Install(stas.Get(i)));

    for(uint32_t i=0; i<nSta; ++i) {
        OnOffHelper dlClient("ns3::UdpSocketFactory", InetSocketAddress(staIf.GetAddress(i), portDl));
        dlClient.SetAttribute("DataRate", DataRateValue(DataRate(std::to_string((int)offeredMbpsDl) + "Mbps")));
        dlClient.SetAttribute("PacketSize", UintegerValue(pktSizeBytes));
        
        // --- UBAH JADI EXPONENTIAL (BURSTY) ---
        // Ini bikin data datang "keroyokan" tapi kecil-kecil -> Scheduler dipaksa kerja (RU aktif)
        dlClient.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.01]"));
        dlClient.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.01]"));
        
        apps.Add(dlClient.Install(ap.Get(0)));

        OnOffHelper ulClient("ns3::UdpSocketFactory", InetSocketAddress(apIf.GetAddress(0), portUl));
        ulClient.SetAttribute("DataRate", DataRateValue(DataRate(std::to_string((int)offeredMbpsUl) + "Mbps")));
        ulClient.SetAttribute("PacketSize", UintegerValue(pktSizeBytes));
        ulClient.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.01]"));
        ulClient.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.01]"));
        apps.Add(ulClient.Install(stas.Get(i)));
    }

    apps.Start(Seconds(1.0));
    apps.Stop(Seconds(simTime));
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    FlowMonitorHelper fmHelper;
    Ptr<FlowMonitor> monitor = fmHelper.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(fmHelper.GetClassifier());
    auto stats = monitor->GetFlowStats();

    std::vector<double> staThroughput(nSta, 0.0);
    double totalDelay = 0;
    long totalRxPackets = 0;

    for (auto const& kv : stats) {
        if (kv.second.rxPackets > 0) {
            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
            double duration = (kv.second.timeLastRxPacket - kv.second.timeFirstRxPacket).GetSeconds();
            if (duration <= 0) duration = 1.0; 
            double tputMbps = (kv.second.rxBytes * 8.0) / duration / 1e6;
            totalDelay += kv.second.delaySum.GetSeconds();
            totalRxPackets += kv.second.rxPackets;
            for(uint32_t i=0; i<nSta; ++i) {
                if(t.destinationAddress == staIf.GetAddress(i) || t.sourceAddress == staIf.GetAddress(i)) {
                    staThroughput[i] += tputMbps; 
                    break;
                }
            }
        }
    }

    double aggThroughput = 0;
    for(double t : staThroughput) aggThroughput += t;
    double avgDelayMs = 0;
    if (totalRxPackets > 0) avgDelayMs = (totalDelay / totalRxPackets) * 1000.0;
    double fairness = JainFairness(staThroughput);

    std::cout << "\n";
    std::cout << "============================================" << std::endl;
    std::cout << "      HASIL SIMULASI (OFDMA RU/MRU)         " << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << " Teknologi       : 802.11" << tech << std::endl;
    std::cout << " Lebar Kanal     : " << width << " MHz" << std::endl;
    std::cout << " Tipe Trafik     : Small Packet (Burst)" << std::endl;
    std::cout << "--------------------------------------------" << std::endl;
    std::cout << std::fixed << std::setprecision(4);
    std::cout << " [1] Agg. Throughput : " << aggThroughput << " Mbps" << std::endl;
    std::cout << " [2] Average Delay   : " << avgDelayMs << " ms" << std::endl;
    std::cout << " [3] Jain's Fairness : " << fairness << " (Index)" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "\n";
    std::cout << "DATA_RU_MRU -> " << aggThroughput << ", " << avgDelayMs << ", " << fairness << std::endl;

    Simulator::Destroy();
    return 0;
}
