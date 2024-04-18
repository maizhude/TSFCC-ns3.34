#include <iostream>
#include <fstream>
#include <chrono>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/netanim-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include <ns3/internet-apps-module.h>
#include <ns3/ofswitch13-module.h>


// Network topology (default)
//
//        n2 n3 n4    n8        .
//         \ | /     /          .
//          \|/     /           .
//     n1--- s1---s2            .
//          /|\     \           .
//         / | \     \          .
//        n5 n6 n7    n9        .
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("YaLing-TOPO");

class OFSwitch13TsfccControllers;

std::stringstream filePlotQueue;
std::stringstream filePlotThroughput1;
std::stringstream filePlotThroughput2;
std::stringstream filePlotfct;

static bool firstRtt = true;
static Ptr<OutputStreamWrapper> rttStream;
static Ptr<OutputStreamWrapper> cWndStream;
static Ptr<OutputStreamWrapper> rWndStream;
static double interval = 0.002;

uint64_t flowRecvBytes1 = 0;
uint64_t flowRecvBytes2 = 0;

static void
RttTracer (Time oldval, Time newval)
{
  if (firstRtt)
    {
      *rttStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRtt = false;
    }
  *rttStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}

static void
CwndTracer(uint32_t oldval, uint32_t newval){
  *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
}

static void
RwndTracer(uint32_t oldval, uint32_t newval){
  *rWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
}

static void
TraceRtt (std::string rtt_tr_file_name)
{
  AsciiTraceHelper ascii;
  rttStream = ascii.CreateFileStream (rtt_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/3/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback (&RttTracer));
}

static void
TraceCwnd (std::string cwnd_tr_file_name)
{
  AsciiTraceHelper ascii;
  cWndStream = ascii.CreateFileStream (cwnd_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/3/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&CwndTracer));
}

static void
TraceRwnd (std::string rwnd_tr_file_name)
{
  AsciiTraceHelper ascii;
  rWndStream = ascii.CreateFileStream (rwnd_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/3/$ns3::TcpL4Protocol/SocketList/0/RWND", MakeCallback (&RwndTracer));
}


void
CheckQueueSize (Ptr<QueueDisc> queue, std::string filePlotQueue)
{
  uint32_t qSize = StaticCast<RedQueueDisc> (queue)->GetNPackets();

  // check queue size every 1/1000 of a second
  Simulator::Schedule (Seconds (0.0001), &CheckQueueSize, queue, filePlotQueue);

  std::ofstream fPlotQueue (filePlotQueue.c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}

void
CheckOfQueueSize (Ptr<OFSwitch13Queue> ofQue, std::string filePlotQueue)
{
  uint32_t qSize = ofQue->GetNPackets();

  // check queue size every 1/1000 of a second
  Simulator::Schedule (Seconds (0.0001), &CheckOfQueueSize, ofQue, filePlotQueue);

  std::ofstream fPlotQueue (filePlotQueue.c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}
/**
 * @brief 为发送端安装应用，用于实验流量生成
 * 
 * @param port 应用的TCP端口
 * @param senders 发送方
 * @param eleReceIpIfaces 象流IP地址
 * @param mouReceIpIfaces 鼠流IP地址
 * @param receiver 接收方
 * @param start_time 开始时间
 * @param stop_time 结束时间
 * @param TCP_SEGMENT TCP片段大小
 * @param data_mbytes 数据总量大小
 * @param sendNum 发送方数量
 */
void InstallApplication(uint16_t port, NodeContainer senders, Ipv4InterfaceContainer eleReceIpIfaces, Ipv4InterfaceContainer mouReceIpIfaces,
                        NodeContainer receiver, double start_time, double stop_time, 
                        uint32_t TCP_SEGMENT, double data_mbytes, uint32_t sendNum){
  // Configure port/address and install application, establish connection
  for (uint32_t i = 0; i < senders.GetN(); ++i) {
    V4PingHelper pingHelper1(mouReceIpIfaces.GetAddress(0)); // 使用服务器的IP地址
    pingHelper1.SetAttribute("Verbose", BooleanValue(false));

    ApplicationContainer singleHostPingApps1 = pingHelper1.Install(senders.Get(i));
    singleHostPingApps1.Start(Seconds(0.1));

    V4PingHelper pingHelper2(eleReceIpIfaces.GetAddress(0)); // 使用服务器的IP地址
    pingHelper2.SetAttribute("Verbose", BooleanValue(false));

    ApplicationContainer singleHostPingApps2 = pingHelper2.Install(senders.Get(i));
    singleHostPingApps2.Start(Seconds(0.3));

    // 将Ping应用程序添加到总的Ping应用程序容器
    // pingApps.Add(singleHostPingApps);
  }
  // long flow
   for(uint16_t i=0; i<10; i++)
  {
	  Address clientAddress(InetSocketAddress(eleReceIpIfaces.GetAddress(0,0), port));
    BulkSendHelper sourceHelper ("ns3::TcpSocketFactory", clientAddress);
    sourceHelper.SetAttribute("SendSize", UintegerValue(TCP_SEGMENT));
    sourceHelper.SetAttribute ("MaxBytes", UintegerValue (data_mbytes*0));
    ApplicationContainer sourceApp = sourceHelper.Install (senders.Get(i));
    sourceApp.Start (Seconds (start_time));
    sourceApp.Stop(Seconds(stop_time));
  }
  // short flow
  for(uint16_t i=10; i<senders.GetN(); i++)
  {
	  Address clientAddress(InetSocketAddress(mouReceIpIfaces.GetAddress(0,0), port));
    BulkSendHelper sourceHelper ("ns3::TcpSocketFactory", clientAddress);
    sourceHelper.SetAttribute("SendSize", UintegerValue(TCP_SEGMENT));
    sourceHelper.SetAttribute ("MaxBytes", UintegerValue (data_mbytes/(sendNum-6)));
    ApplicationContainer sourceApp = sourceHelper.Install (senders.Get(i));
    sourceApp.Start (Seconds (start_time+0.3));
  }

}

void QuerySketch(Ptr<OFSwitch13Device> openFlowDev){
  openFlowDev->GetSketchData();

  // Reschedule the function call
  Time delay = MilliSeconds(10); // Set the desired time interval
  Simulator::Schedule(delay, &QuerySketch, openFlowDev);
}

/**
 * @brief 定时查询交换机队列长度
 * 
 * @param openFlowDev OFSwitch13Device
 */
void QueryAllQueLength(Ptr<OFSwitch13Device> openFlowDev) {
  //获取交换机的端口数量
  size_t portSize = openFlowDev->GetSwitchPortSize();
  uint64_t dpid = openFlowDev->GetDpId();
  for(uint32_t i = 0; i < portSize; i++){
    Ptr<OFSwitch13Port> ofPort = openFlowDev->GetSwitchPort(i+1);
    Ptr<OFSwitch13Queue> ofQue = ofPort->GetPortQueue();
    uint16_t queueLength = ofQue->GetNPackets();
    uint16_t state = ofPort->GetCongestionState();
    uint16_t count = ofPort->GetCongestionRecoverCount();
    uint32_t port_no = i+1;
    //判断是否大于阈值
    if(queueLength > 60){
      // NS_LOG_INFO("The Port " << i+1 << " queueLength is " << queueLength);
      if(state == 0){
        state = 1;
        ofPort->SetCongestionState(state);
      }
      openFlowDev->SendQueueCongestionNotifyMessage(dpid,queueLength,port_no);
      count = 0;
      ofPort->SetCongestionRecoverCount(count);
    }else{
      if(state == 1){
        count++;
        ofPort->SetCongestionRecoverCount(count);
      }
      if(count == 3){
        openFlowDev->SendQueueCongestionRecoverMessage(dpid,queueLength,port_no);
        // NS_LOG_INFO("The count is " << count);
        count = 0;
        state = 0;
        ofPort->SetCongestionRecoverCount(count);
        ofPort->SetCongestionState(state);
      }
    }
    //OFSwitch13Device构造发送函数，发送到控制器
  }
  
  // Reschedule the function call
  Time delay = MicroSeconds(10); // Set the desired time interval
  Simulator::Schedule(delay, &QueryAllQueLength, openFlowDev);
}
void
ThroughputPerSecond1(Ptr<PacketSink> sinkApps,std::string filePlotThrough)
{
	uint32_t totalRecvBytes = sinkApps->GetTotalRx();
	uint32_t currentPeriodRecvBytes = totalRecvBytes - flowRecvBytes1;

	flowRecvBytes1 = totalRecvBytes;

	Simulator::Schedule (Seconds(interval), &ThroughputPerSecond1, sinkApps, filePlotThrough);
	std::ofstream fPlotThroughput (filePlotThrough.c_str (), std::ios::out | std::ios::app);
	fPlotThroughput << Simulator::Now().GetSeconds() << " " << currentPeriodRecvBytes * 8 / (interval * 1000000) << std::endl;
}

void
ThroughputPerSecond2(Ptr<PacketSink> sinkApps,std::string filePlotThrough)
{
	uint32_t totalRecvBytes = sinkApps->GetTotalRx();
	uint32_t currentPeriodRecvBytes = totalRecvBytes - flowRecvBytes2;

	flowRecvBytes2 = totalRecvBytes;

	Simulator::Schedule (Seconds(interval), &ThroughputPerSecond2, sinkApps, filePlotThrough);
	std::ofstream fPlotThroughput (filePlotThrough.c_str (), std::ios::out | std::ios::app);
	fPlotThroughput << Simulator::Now().GetSeconds() << " " << currentPeriodRecvBytes * 8 / (interval * 1000000) << std::endl;
}


int main (int argc, char *argv[])
{
  auto simStart = std::chrono::high_resolution_clock::now();
  // Configure information
  std::string prefix_file_name = "TSFCC-YaLing-TOPO";
  uint32_t sendNum = 90;//(1~50)
  std::string transport_port = "TcpCubic";// TcpDctcp  TcpCubic
  std::string queue_disc_type = "RedQueueDisc";//only for dctcp

  // todo
  bool is_sdn = true;// is SDN enabled?

  std::string queue_limit = "250p";//94
  double K = 65;//18

  std::string bandwidth = "10Gbps";
  uint64_t delay = 10;
  std::string bottleneck_bw = "10Gbps";
  uint64_t bottleneck_delay = 10;
  if(transport_port == "TcpDctcp"){
    delay = 80;
    bottleneck_delay = 80;
  }
  uint32_t IP_PACKET_SIZE = 1500;
  uint32_t TCP_SEGMENT = IP_PACKET_SIZE-40;
  uint32_t flow_size = 128;//KB
  double data_mbytes;

  double minRto = 25;
  uint32_t initialCwnd = 2;

  double start_time = 0.7;
  double stop_time = 1.2;

  bool tracing = true;

  // Create directory information
 time_t rawtime;
 struct tm * timeinfo;
 char buffer[80];
 time (&rawtime);
 timeinfo = localtime(&rawtime);
//
 strftime(buffer,sizeof(buffer),"%d-%m-%Y-%I-%M-%S",timeinfo);
 std::string currentTime (buffer);

  CommandLine cmd;
  cmd.AddValue ("sendNum","Number of left and right side leaf nodes", sendNum);
  cmd.AddValue ("tracing","Whether to log and track information", tracing);
  cmd.AddValue ("queuedisc","type of queuedisc", queue_disc_type);
  cmd.AddValue ("bandwidth", "Access bandwidth", bandwidth);
  cmd.AddValue ("delay", "Access delay", delay);
  cmd.AddValue ("bottleneck_bw", "Bottleneck bandwidth", bottleneck_bw);
  cmd.AddValue ("bottleneck_delay", "Bottleneck delay", bottleneck_delay);
  cmd.AddValue ("TCP_SEGMENT", "Packet size", TCP_SEGMENT);
  cmd.AddValue ("data", "Number of Megabytes of data to transmit, 0 means infinite", data_mbytes);
  cmd.AddValue ("start_time", "Start Time", start_time);
  cmd.AddValue ("stop_time", "Stop Time", stop_time);
  cmd.AddValue ("initialCwnd", "Initial Cwnd", initialCwnd);
  cmd.AddValue ("duplex",   "ns3::CsmaChannel::FullDuplex");
  cmd.AddValue ("minRto", "Minimum RTO", minRto);
  cmd.AddValue ("is_sdn", "Whether to enable the Openflow protocol", is_sdn);
  cmd.AddValue ("transport_port", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpDctcp, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
                "TcpLp, TcpBbr", transport_port);
  cmd.Parse (argc,argv);

  // To enable logging
  LogComponentEnable("YaLing-TOPO", LOG_LEVEL_INFO);
  LogComponentEnable("OFSwitch13TsfccController", LOG_LEVEL_WARN);


  NS_LOG_INFO ("Configure TcpSocket");
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + transport_port));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (minRto)));
  Config::SetDefault ("ns3::TcpSocketBase::Timestamp", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (true));
  // Config::SetDefault ("ns3::RttMeanDeviation::Alpha", DoubleValue (1));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1460));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (initialCwnd));
  Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (100)));

  if(transport_port.compare("TcpDctcp") == 0 || transport_port.compare("TcpDstcp") == 0)
  {
	  NS_LOG_INFO ("Configure ECN and RED");
	  Config::SetDefault ("ns3::"+queue_disc_type+"::UseEcn", BooleanValue (true));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::MaxSize", QueueSizeValue (QueueSize (queue_limit)));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::MeanPktSize", UintegerValue (IP_PACKET_SIZE));
	  Config::SetDefault("ns3::"+queue_disc_type+"::QW", DoubleValue (1));
	  Config::SetDefault("ns3::"+queue_disc_type+"::MinTh", DoubleValue (K));
	  Config::SetDefault("ns3::"+queue_disc_type+"::MaxTh", DoubleValue (K));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::Gentle", BooleanValue (false));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::UseHardDrop", BooleanValue (false));
  }
  else
  {
	  NS_LOG_INFO ("Configure Fifo");
	  queue_disc_type = "FifoQueueDisc";
	  Config::SetDefault("ns3::FifoQueueDisc::MaxSize", QueueSizeValue (QueueSize (queue_limit)));
  }
  data_mbytes = (sendNum-6)*flow_size*1024;
  /******** Create Nodes ********/
  NodeContainer switches;
  switches.Create(2);//哑铃结构
  NodeContainer senders;
  senders.Create(sendNum);
  NodeContainer receiver;
  receiver.Create(2);//两个接收端，一个接收大象流，一个接收老鼠流
  NS_LOG_INFO ("--------" << senders.Get(0)->GetId ());
  NS_LOG_INFO ("--------" << senders.Get(1)->GetId ());
  NetDeviceContainer hostDevices;
  NetDeviceContainer switch1Ports;
  NetDeviceContainer switch2Ports;
  NetDeviceContainer mouReceDevices;
  NetDeviceContainer eleReceDevices;

  Ipv4InterfaceContainer eleReceIpIfaces;
  Ipv4InterfaceContainer mouReceIpIfaces;
  QueueDiscContainer queueDiscs;
  Ptr<OFSwitch13Device> openFlowDev1;
  Ptr<OFSwitch13Device> openFlowDev2;

  /******** Create Channels ********/
  CsmaHelper csmaHelper;
  csmaHelper.SetChannelAttribute("DataRate", DataRateValue (DataRate (bandwidth)));
  csmaHelper.SetChannelAttribute("Delay", TimeValue (MicroSeconds (delay)));
  csmaHelper.SetChannelAttribute("FullDuplex", BooleanValue(true));
  CsmaHelper csmaNeckLink;
  csmaNeckLink.SetChannelAttribute("DataRate", DataRateValue (DataRate (bottleneck_bw)));
  csmaNeckLink.SetChannelAttribute("Delay", TimeValue (MicroSeconds (bottleneck_delay)));
  csmaNeckLink.SetChannelAttribute("FullDuplex", BooleanValue(true));
  if(is_sdn == true){//采用OpenFlow，必须是TCPCubic
    NS_ASSERT_MSG (transport_port == "TcpCubic", "使用SDN实验必须采用CUBIC协议——The CUBIC protocol must be used in SDN experiments");
    /******** Install Internet Stack ********/
    InternetStackHelper stack;
    stack.Install (senders);
    stack.Install (receiver);

    // Configure Ipv4AddressHelper
    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");

    /******** Create NetDevices ********/
    for(uint32_t i = 0; i<sendNum; i++)
    {
      NodeContainer pair (senders.Get(i), switches.Get(0));
      NetDeviceContainer link = csmaHelper.Install (pair);
      switch1Ports.Add(link.Get(1));
      hostDevices.Add (link.Get (0));
    }

    // Connect switches1 with switches2
    NodeContainer switchPair (switches.Get(0), switches.Get(1));
    NetDeviceContainer switchLink = csmaNeckLink.Install (switchPair);
    switch1Ports.Add(switchLink.Get(0));
    switch2Ports.Add(switchLink.Get(1));

    // Connect switches with receiver
    NodeContainer mouPair (switches.Get(1), receiver.Get(0));
    NetDeviceContainer mouLink = csmaHelper.Install (mouPair);
    switch2Ports.Add(mouLink.Get(0));
    mouReceDevices.Add (mouLink.Get(1));

    NodeContainer elePair (switches.Get(1), receiver.Get(1));
    NetDeviceContainer eleLink = csmaHelper.Install (elePair);
    switch2Ports.Add(eleLink.Get(0));
    eleReceDevices.Add (eleLink.Get(1));


    /******** Set IP addresses of the nodes in the network ********/
    Ipv4InterfaceContainer hostIpIfaces = address.Assign (hostDevices);
    mouReceIpIfaces = address.Assign (mouReceDevices);
    eleReceIpIfaces = address.Assign (eleReceDevices);

    Ptr<Node> controllerNode = CreateObject<Node> ();

    // 利用OFSwitch13InternalHelper配置openflow，安装上控制器和交换机，然后创建openflow信道
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper> ();
    of13Helper->InstallController (controllerNode);
    openFlowDev1 = of13Helper->InstallSwitch (switches.Get(0), switch1Ports);
    openFlowDev2 = of13Helper->InstallSwitch (switches.Get(1), switch2Ports);
    of13Helper->CreateOpenFlowChannels ();
    // OFSwitch13Helper::EnableDatapathLogs ();
  }else {//不采用OpenFlow，可以是TCPCubic或DCTCP等
    /******** Install Internet Stack ********/
    InternetStackHelper stack;
    stack.InstallAll();

    /******** Configure TrafficControlHelper ********/
    TrafficControlHelper tchRed;
    tchRed.SetRootQueueDisc("ns3::"+queue_disc_type);
    NS_LOG_INFO ("Install " << queue_disc_type);

    // Configure Ipv4AddressHelper
    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");

    /******** Create NetDevices ********/
    Ipv4InterfaceContainer hostInterfaces;
    for(uint32_t i = 0; i<sendNum; i++)
    {
      NodeContainer pair (senders.Get(i), switches.Get(0));
      NetDeviceContainer link = csmaHelper.Install (pair);
      hostDevices.Add (link.Get (0));
      tchRed.Install(link);
      address.NewNetwork();
      Ipv4InterfaceContainer interfaces = address.Assign(link);
      hostInterfaces.Add(interfaces.Get(0));
    }

    // Connect switches with receiver
    NodeContainer switchPair (switches.Get(0), switches.Get(1));
    NetDeviceContainer link = csmaNeckLink.Install (switchPair);
    // Install queueDiscs in switch
    queueDiscs = tchRed.Install(link);

    /******** Set IP addresses of the nodes in the network ********/
    address.NewNetwork();
    address.Assign(link);

    NodeContainer mouPair (switches.Get(1), receiver.Get(0));
    NetDeviceContainer mouLink = csmaHelper.Install (mouPair);
    tchRed.Install(mouLink);
    address.NewNetwork();
    Ipv4InterfaceContainer mouInterfaces = address.Assign(mouLink);
    mouReceIpIfaces.Add(mouInterfaces.Get(1));

    NodeContainer elePair (switches.Get(1), receiver.Get(1));
    NetDeviceContainer eleLink = csmaHelper.Install (elePair);
    address.NewNetwork();
    tchRed.Install(eleLink);
    Ipv4InterfaceContainer eleInterfaces = address.Assign(eleLink);
    eleReceIpIfaces.Add(eleInterfaces.Get(1));

    // Configure routing
    NS_LOG_INFO ("Initialize Global Routing.");
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
  }
  /******** Create Sender and Receiver Apps on End-hosts ********/
  NS_LOG_INFO ("Build connections");
  uint16_t port = 50000;
  InstallApplication(port, senders, eleReceIpIfaces, mouReceIpIfaces, receiver, start_time, stop_time , TCP_SEGMENT, data_mbytes, sendNum);
  
  Address mouServerAddress(InetSocketAddress(Ipv4Address::GetAny(), port));
  PacketSinkHelper mouPacketSinkHelper("ns3::TcpSocketFactory", mouServerAddress);
  ApplicationContainer mouSinkApps = mouPacketSinkHelper.Install(receiver.Get(0));
  mouSinkApps.Start(Seconds(start_time));
  mouSinkApps.Stop(Seconds(stop_time));

  Address eleServerAddress(InetSocketAddress(Ipv4Address::GetAny(), port));
  PacketSinkHelper elePacketSinkHelper("ns3::TcpSocketFactory", eleServerAddress);
  ApplicationContainer eleSinkApps = elePacketSinkHelper.Install(receiver.Get(1));
  eleSinkApps.Start(Seconds(start_time));
  eleSinkApps.Stop(Seconds(stop_time));
  
  /******** Set the message traces for the clients ********/
  std::string dir;
  std::string pcap_dir;
  if(is_sdn == true && tracing){
    dir = "yaling/openflow/"+std::to_string(sendNum)+ "/" +std::to_string(flow_size)+ "KB/" + transport_port.substr(0, transport_port.length()) + "/" + currentTime + "/";
    std::cout << "Data directory:" << dir << std::endl;
    pcap_dir = "yaling-pcap/openflow/"+std::to_string(sendNum)+ "/" +std::to_string(flow_size)+ "KB/" + transport_port.substr(0, transport_port.length()) + "/" + currentTime + "/";
    std::cout << "Pcap directory:" << pcap_dir << std::endl;
  }else{
    dir = "yaling/"+std::to_string(sendNum)+ "/" +std::to_string(flow_size)+ "KB/" + transport_port.substr(0, transport_port.length()) + "/" + currentTime + "/";
    std::cout << "Data directory:" << dir << std::endl;
    pcap_dir = "yaling-pcap/"+std::to_string(sendNum)+ "/" +std::to_string(flow_size)+ "KB/" + transport_port.substr(0, transport_port.length()) + "/" + currentTime + "/";
    std::cout << "Pcap directory:" << pcap_dir << std::endl;
  }
  if (tracing)
  {
    std::string dirToSave = "mkdir -p " + dir;
    system (dirToSave.c_str ());
    std::string pcapDirToSave = "mkdir -p " + pcap_dir;
    system (pcapDirToSave.c_str ());

	Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRtt, dir+"/rtt.data");
    Simulator::Schedule (Seconds (start_time + 0.000001), &TraceCwnd, dir+"/cwnd.data");
    Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRwnd, dir+"/rwnd.data");
    // of13Helper->EnableOpenFlowPcap ("openflow");
    // of13Helper->EnableDatapathStats ("switch-stats");
    // Get queue size
    filePlotQueue <<  dir << "/" << "queue-size.plotme";
    if(is_sdn == true){
      // csmaHelper.EnablePcap (pcap_dir+"/switch", switch1Ports, true);
      // csmaHelper.EnablePcap (pcap_dir+"/switch", switch2Ports, true);

      size_t portSize = openFlowDev1->GetSwitchPortSize();
      Ptr<OFSwitch13Port> ofPort = openFlowDev1->GetSwitchPort(portSize);
      Ptr<OFSwitch13Queue> ofQue = ofPort->GetPortQueue();
      Simulator::ScheduleNow (&CheckOfQueueSize, ofQue, filePlotQueue.str());
    }else{
      Ptr<QueueDisc> queue = queueDiscs.Get(0);
      Simulator::ScheduleNow (&CheckQueueSize, queue, filePlotQueue.str());
    }
    // csmaHelper.EnablePcap (pcap_dir+"/mouReceive", mouReceDevices);
    // csmaHelper.EnablePcap (pcap_dir+"/eleReceive", eleReceDevices);
    // csmaHelper.EnablePcap (pcap_dir+"/csmaHost", hostDevices);
    // Get throughput1
    filePlotThroughput1  <<  dir << "/"  << "throughput1.dat";
    remove (filePlotThroughput1.str ().c_str());
    Simulator::ScheduleNow (&ThroughputPerSecond1, mouSinkApps.Get(0)->GetObject<PacketSink>(), filePlotThroughput1.str ());

    // Get throughput2
    filePlotThroughput2  <<  dir << "/"  << "throughput2.dat";
    remove (filePlotThroughput2.str ().c_str());
    Simulator::ScheduleNow (&ThroughputPerSecond2, eleSinkApps.Get(0)->GetObject<PacketSink>(), filePlotThroughput2.str ());
  }
  /******** Enable openflow switch queue detection ********/
  if(is_sdn == true){
    Time initialQueueDelay = MicroSeconds(10); // Initial delay before the first execution
    Time initialSketchDelay = MilliSeconds(10);
    Simulator::Schedule(initialQueueDelay, &QueryAllQueLength, openFlowDev1);
    Simulator::Schedule(initialSketchDelay, &QuerySketch, openFlowDev1);
  }

  // Install FlowMonitor on all nodes
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  //AnimationInterface anim("dctcpVScubic.xml");
  Simulator::Stop (Seconds(stop_time));
  Simulator::Run ();

  // Get information from FlowMonitor
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
  filePlotfct << dir << "/" << "FCT.dat";
  remove (filePlotfct.str ().c_str());
  std::ofstream fPlotFCT (filePlotfct.str ().c_str(), std::ios::out | std::ios::app);
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
  {
    // Ipv4FlowClassifier::FiveTuple FiveTuple = classifier->FindFlow (i->first);
    // fPlotFCT  << "Flow " << i->first << " (" << FiveTuple.sourceAddress << " -> " << FiveTuple.destinationAddress << ")\n";
    // fPlotFCT  << "  Statr time: " << i->second.timeFirstTxPacket << "\n";
    // fPlotFCT  << "  Tx Packets: " << i->second.txPackets << "\n";
    // fPlotFCT  << "  Tx Bytes:   " << i->second.txBytes << "\n";
    // fPlotFCT  << "  TxOffered:  " << i->second.txBytes * 8.0 / 1000000 / (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
    // fPlotFCT  << "  Rx Packets: " << i->second.rxPackets << "\n";
    // fPlotFCT  << "  Rx Bytes:   " << i->second.rxBytes << "\n";
    // fPlotFCT  << "  Throughput: " << i->second.rxBytes * 8.0 / 1000000/ (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
    fPlotFCT  << (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetMilliSeconds() << "\n";
  }

  // Collect data
  NS_LOG_INFO ("Collect data.");

  Simulator::Destroy ();

  /***** Measure the actual time the simulation has taken (for reference) *****/
  auto simStop = std::chrono::high_resolution_clock::now();
  auto simTime =
      std::chrono::duration_cast<std::chrono::milliseconds>(simStop - simStart);
  double simTimeMin = (double)simTime.count() / 1e3 / 60;
  NS_LOG_UNCOND("Time taken by simulation: " << simTimeMin << " minutes");

  return 0;
}