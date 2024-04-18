#include <iostream>
#include <iomanip>
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

#include <stdio.h>
#include <stdlib.h>

#define TG_CDF_TABLE_ENTRY 32
#define PORT_START 10000
#define PORT_END 50000
struct cdf_entry
{
    double value;
    double cdf;
};

/* CDF distribution */
struct cdf_table
{
    struct cdf_entry *entries;
    int num_entry;  /* number of entries in CDF table */
    int max_entry;  /* maximum number of entries in CDF table */
    double min_cdf; /* minimum value of CDF (default 0) */
    double max_cdf; /* maximum value of CDF (default 1) */
};

/* initialize a CDF distribution */
void init_cdf(struct cdf_table *table)
{
    if(!table)
        return;

    table->entries = (struct cdf_entry*)malloc(TG_CDF_TABLE_ENTRY * sizeof(struct cdf_entry));
    table->num_entry = 0;
    table->max_entry = TG_CDF_TABLE_ENTRY;
    table->min_cdf = 0;
    table->max_cdf = 1;

    if (!(table->entries))
        perror("Error: malloc entries in init_cdf()");
}

/* free resources of a CDF distribution */
void free_cdf(struct cdf_table *table)
{
    if (table)
        free(table->entries);
}

/* get CDF distribution from a given file */
void load_cdf(struct cdf_table *table, const char *file_name)
{
    FILE *fd = NULL;
    char line[256] = {0};
    struct cdf_entry *e = NULL;
    int i = 0;

    if (!table)
        return;

    fd = fopen(file_name, "r");
    if (!fd)
        perror("Error: open the CDF file in load_cdf()");

    while (fgets(line, sizeof(line), fd))
    {
        /* resize entries */
        if (table->num_entry >= table->max_entry)
        {
            table->max_entry *= 2;
            e = (struct cdf_entry*)malloc(table->max_entry * sizeof(struct cdf_entry));
            if (!e)
                perror("Error: malloc entries in load_cdf()");
            for (i = 0; i < table->num_entry; i++)
                e[i] = table->entries[i];
            free(table->entries);
            table->entries = e;
        }

        sscanf(line, "%lf %lf", &(table->entries[table->num_entry].value), &(table->entries[table->num_entry].cdf));

        if (table->min_cdf > table->entries[table->num_entry].cdf)
            table->min_cdf = table->entries[table->num_entry].cdf;
        if (table->max_cdf < table->entries[table->num_entry].cdf)
            table->max_cdf = table->entries[table->num_entry].cdf;

        table->num_entry++;
    }
    fclose(fd);
}

/* print CDF distribution information */
void print_cdf(struct cdf_table *table)
{
    int i = 0;

    if (!table)
        return;

    for (i = 0; i < table->num_entry; i++)
        printf("%.2f %.2f\n", table->entries[i].value, table->entries[i].cdf);
}

/* get average value of CDF distribution */
double avg_cdf(struct cdf_table *table)
{
    int i = 0;
    double avg = 0;
    double value, prob;

    if (!table)
        return 0;

    for (i = 0; i < table->num_entry; i++)
    {
        if (i == 0)
        {
            value = table->entries[i].value / 2;
            prob = table->entries[i].cdf;
        }
        else
        {
            value = (table->entries[i].value + table->entries[i-1].value) / 2;
            prob = table->entries[i].cdf - table->entries[i-1].cdf;
        }
        avg += (value * prob);
    }

    return avg;
}

double interpolate(double x, double x1, double y1, double x2, double y2)
{
    if (x1 == x2)
        return (y1 + y2) / 2;
    else
        return y1 + (x - x1) * (y2 - y1) / (x2 - x1);
}

/* generate a random floating point number from min to max */
double rand_range(double min, double max)
{
    return min + rand() * (max - min) / RAND_MAX;
}

/* generate a random value based on CDF distribution */
double gen_random_cdf(struct cdf_table *table)
{
    int i = 0;
    double x = rand_range(table->min_cdf, table->max_cdf);
    /* printf("%f %f %f\n", x, table->min_cdf, table->max_cdf); */

    if (!table)
        return 0;

    for (i = 0; i < table->num_entry; i++)
    {
        if (x <= table->entries[i].cdf)
        {
            if (i == 0)
                return interpolate(x, 0, 0, table->entries[i].cdf, table->entries[i].value);
            else
                return interpolate(x, table->entries[i-1].cdf, table->entries[i-1].value, table->entries[i].cdf, table->entries[i].value);
        }
    }

    return table->entries[table->num_entry-1].value;
}
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TSFCC-SINGLE_TREE-TOPO");

class OFSwitch13TsfccControllers;

std::stringstream filePlotfct;

double poission_gen_interval(double avg_rate)
{
    if (avg_rate > 0)
       return -logf(1.0 - (double)rand() / RAND_MAX) / avg_rate;
    else
       return 0;
}

template<typename T>
T rand_range (T min, T max)
{
    return min + ((double)max - min) * rand () / RAND_MAX;
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
 * @param hostNodes 终端节点
 * @param eleReceIpIfaces 象流IP地址
 * @param mouReceIpIfaces 鼠流IP地址
 * @param START_TIME 开始时间
 * @param stop_time 结束时间
 * @param TCP_SEGMENT TCP片段大小
 * @param nHosts 终端数量
 */
void InstallApplication(uint32_t fromPodId, uint32_t serverCount, uint32_t k, NodeContainer &hostNodes, 
                        Ipv4InterfaceContainer *hostToTorIfs, double START_TIME, double END_TIME, 
                        double FLOW_LAUNCH_END_TIME, struct cdf_table *cdfTable, double requestRate,uint32_t TCP_SEGMENT, 
                        uint32_t nHosts, long &flowCount, long &totalFlowSize){
  // Configure port/address and install application, establish connection
  for (uint32_t i = 0; i < nHosts; ++i) {
    for (uint32_t j = 0; j < nHosts; ++j) {
        if (i != j) {
            // 创建 ping 应用程序，`i` 对 `j` 进行 ping
            V4PingHelper pingHelper(hostToTorIfs[j].GetAddress(0));
            pingHelper.SetAttribute("Verbose", BooleanValue(false));

            ApplicationContainer pingApp = pingHelper.Install(hostNodes.Get(i));
            pingApp.Start(Seconds(0.1));  // 设置启动时间
            
        }
    }
  }
  //固定大象流生成
  for (uint32_t i = 0; i < serverCount * (k / 2); ++i) {
    uint32_t fromServerIndex = fromPodId * serverCount * (k / 2) + i;
    uint16_t port = PORT_END;
    // Install packet sinks
    PacketSinkHelper sink ("ns3::TcpSocketFactory",
            InetSocketAddress (Ipv4Address::GetAny (), port));
    ApplicationContainer sinkApp = sink.Install (hostNodes.Get (fromServerIndex));
    sinkApp.Start (Seconds (START_TIME));
    sinkApp.Stop (Seconds (END_TIME));
    uint32_t destServerIndex = fromServerIndex;
    while (destServerIndex >= fromPodId * serverCount * (k / 2)
            && destServerIndex < (fromPodId + 1) * serverCount * (k / 2))

    {
      destServerIndex = rand_range (0u, serverCount * (k / 2) * k);
    }
    Ptr<Node> destServer = hostNodes.Get (destServerIndex);
    Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4> ();
    Ipv4InterfaceAddress destInterface = ipv4->GetAddress (1, 0);
    Ipv4Address destAddress = destInterface.GetLocal ();

      BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (destAddress, port));

      source.SetAttribute ("SendSize", UintegerValue (TCP_SEGMENT));
      source.SetAttribute ("MaxBytes", UintegerValue(0));

      // Install apps
      ApplicationContainer sourceApp = source.Install (hostNodes.Get (fromServerIndex));
      sourceApp.Start (Seconds (START_TIME));
      sourceApp.Stop (Seconds (END_TIME));

  }
  NS_LOG_INFO ("Install applications:");
  //随机流量生成
  for (uint32_t i = 0; i < serverCount * (k / 2); ++i) {

    uint32_t fromServerIndex = fromPodId * serverCount * (k / 2) + i;
    uint16_t port = PORT_END;

    double startTime = START_TIME + 0.3 + poission_gen_interval (requestRate);

    while (startTime < FLOW_LAUNCH_END_TIME + 0.3)
    {
      flowCount ++;
      uint32_t destServerIndex = fromServerIndex;
      while (destServerIndex >= fromPodId * serverCount * (k / 2)
              && destServerIndex < (fromPodId + 1) * serverCount * (k / 2))

      {
          destServerIndex = rand_range (0u, serverCount * (k / 2) * k);
      }
          Ptr<Node> destServer = hostNodes.Get (destServerIndex);
	        Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4> ();
	        Ipv4InterfaceAddress destInterface = ipv4->GetAddress (1, 0);
	        Ipv4Address destAddress = destInterface.GetLocal ();

            BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (destAddress, port));
            //todo
            uint32_t flowSize = gen_random_cdf (cdfTable);

            totalFlowSize += flowSize;
 	          source.SetAttribute ("SendSize", UintegerValue (TCP_SEGMENT));
            source.SetAttribute ("MaxBytes", UintegerValue(flowSize));

            // Install apps
            ApplicationContainer sourceApp = source.Install (hostNodes.Get (fromServerIndex));
            sourceApp.Start (Seconds (startTime));
            sourceApp.Stop (Seconds (END_TIME));

            // NS_LOG_INFO ("\tFlow from server: " << fromServerIndex << " to server: "
            //         << destServerIndex << " on port: " << port << " with flow size: "
            //         << flowSize << " [start time: " << startTime <<"]");

            startTime += poission_gen_interval (requestRate);
    }
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
int main (int argc, char *argv[])
{
  auto simStart = std::chrono::high_resolution_clock::now();
  // Configure information
  std::string prefix_file_name = "TSFCC-Single-Tree-TOPO";
  std::string transport_port = "TcpCubic";// TcpDctcp  TcpCubic
  std::string queue_disc_type = "RedQueueDisc";//only for dctcp

  uint32_t nHosts = 64;
  uint32_t nTors = 8;
  uint32_t serverCount = nHosts/nTors;
  uint32_t nLeafSw = 4;
  uint32_t nSpineSw = 1;
  uint32_t nLeafPerSpine = nLeafSw/nSpineSw;
  double load = 1.0;
  
  std::string queue_limit = "250p";//94

  std::string core_bw = "40Gbps";
  uint64_t core_delay = 10;
  std::string bottleneck_bw = "10Gbps";
  uint64_t bottleneck_delay = 10;

  uint32_t IP_PACKET_SIZE = 1500;
  uint32_t TCP_SEGMENT = IP_PACKET_SIZE-40;
  unsigned randomSeed = 1;

  double minRto = 25;
  uint32_t initialCwnd = 2;

  double START_TIME = 0.5;
  double END_TIME = 2.0;//1
  double FLOW_LAUNCH_END_TIME = 0.51;//0.1

  std::string cdfFileName = "/home/maizhudelaoyeye/workspace/ns-allinone-3.34/ns-3.34/scratch/DCTCP_CDF.txt";//shortflow_CDF.txt, CDF_webSearch.txt， CDF_dataMining.txt, CDF_Hadoop.txt
  
  // Create directory information
  time_t rawtime;
  struct tm * timeinfo;
  char buffer[80];
  time (&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer,sizeof(buffer),"%d-%m-%Y-%I-%M-%S",timeinfo);
  std::string currentTime (buffer);
  NS_ASSERT_MSG (transport_port == "TcpCubic", "The CUBIC protocol must be used in SDN experiments");

  CommandLine cmd;
  cmd.AddValue ("queuedisc","type of queuedisc", queue_disc_type);
  cmd.AddValue ("core_bw", "Access bandwidth", core_bw);
  cmd.AddValue ("core_delay", "Access delay", core_delay);
  cmd.AddValue ("bottleneck_bw", "Bottleneck bandwidth", bottleneck_bw);
  cmd.AddValue ("bottleneck_delay", "Bottleneck delay", bottleneck_delay);
  cmd.AddValue ("TCP_SEGMENT", "Packet size", TCP_SEGMENT);
  cmd.AddValue ("start_time", "Start Time", START_TIME);
  cmd.AddValue ("end_time", "Stop Time", END_TIME);
  cmd.AddValue ("initialCwnd", "Initial Cwnd", initialCwnd);
  cmd.AddValue ("duplex",   "ns3::CsmaChannel::FullDuplex");
  cmd.AddValue ("minRto", "Minimum RTO", minRto);
  cmd.AddValue ("transport_port", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpDctcp, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
                "TcpLp, TcpBbr", transport_port);
  cmd.Parse (argc,argv);
  // To enable logging
  LogComponentEnable("TSFCC-SINGLE_TREE-TOPO", LOG_LEVEL_INFO);
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

  NS_LOG_INFO ("Configure Fifo");
  queue_disc_type = "FifoQueueDisc";
  Config::SetDefault("ns3::FifoQueueDisc::MaxSize", QueueSizeValue (QueueSize (queue_limit)));
  /******** Create Nodes ********/
  NS_LOG_DEBUG("Creating Nodes...");
  NodeContainer hostNodes;
  hostNodes.Create(nHosts);

  NodeContainer torNodes;
  torNodes.Create(nTors);

  NodeContainer leafNodes;
  leafNodes.Create(nLeafSw);

  NodeContainer spineNodes;
  spineNodes.Create(nSpineSw);

  /******** Create Channels ********/
  NS_LOG_DEBUG("Configuring Channels...");

  CsmaHelper hostLinks;
  hostLinks.SetChannelAttribute("DataRate", DataRateValue (DataRate (bottleneck_bw)));
  hostLinks.SetChannelAttribute("Delay", TimeValue (MicroSeconds (bottleneck_delay)));
  hostLinks.SetChannelAttribute("FullDuplex", BooleanValue(true));

  CsmaHelper coreLinks;
  coreLinks.SetChannelAttribute("DataRate", DataRateValue (DataRate (core_bw)));
  coreLinks.SetChannelAttribute("Delay", TimeValue (MicroSeconds (core_delay)));
  coreLinks.SetChannelAttribute("FullDuplex", BooleanValue(true));
  
  /******** Create NetDevices ********/
  NS_LOG_DEBUG("Creating NetDevices...");
  NetDeviceContainer hostDevices[nHosts];
  NetDeviceContainer torDevices[nTors];
  for (uint32_t i = 0; i < nHosts; i++) {
    NodeContainer pair (hostNodes.Get(i), torNodes.Get(i / (nHosts / nTors)));
    NetDeviceContainer link = hostLinks.Install (pair);
    torDevices[i / (nHosts / nTors)].Add(link.Get(1));
    hostDevices[i].Add (link.Get (0));
  }

  NetDeviceContainer leafDevices[nLeafSw];
  for (uint32_t i = 0; i < nTors; i++) {
    NodeContainer pair (torNodes.Get(i), leafNodes.Get(i / (nTors / nLeafSw)));
    NetDeviceContainer link = hostLinks.Install (pair);
    leafDevices[i / (nTors / nLeafSw)].Add(link.Get(1));
    torDevices[i].Add (link.Get (0));
  }

  NetDeviceContainer spineDevices[nSpineSw];
  for (uint32_t i = 0; i < nLeafSw; i++) {
    NodeContainer pair (leafNodes.Get(i), spineNodes.Get(i / (nLeafSw / nSpineSw)));
    NetDeviceContainer link = coreLinks.Install (pair);
    spineDevices[i / (nLeafSw / nSpineSw)].Add(link.Get(1));
    leafDevices[i].Add (link.Get (0));
  }

  /******** Install Internet Stack ********/
  NS_LOG_DEBUG("Installing Internet Stack...");

  InternetStackHelper stack;
  stack.Install(hostNodes);

  /******** Set IP addresses of the nodes in the network ********/
  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.255.255.0");

  std::vector<InetSocketAddress> hostAddresses;
  Ipv4InterfaceContainer hostToTorIfs[nHosts];
  for (uint32_t i = 0; i < nHosts; i++) {
    hostToTorIfs[i] = address.Assign(hostDevices[i]);
  }
  /******** Ser OpenFlow Switches and Controller in the network  ********/
  Ptr<Node> controllerNode = CreateObject<Node> ();

  // 利用OFSwitch13InternalHelper配置openflow，安装上控制器和交换机，然后创建openflow信道
  Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper> ();
  of13Helper->InstallController (controllerNode);
  std::vector<Ptr<OFSwitch13Device>> openFlowDevs;
  for (uint32_t i = 0; i < nTors; i++) {
    Ptr<OFSwitch13Device> openFlowDev = of13Helper->InstallSwitch (torNodes.Get(i), torDevices[i]);
    openFlowDevs.push_back(openFlowDev);
  }
  for (uint32_t i = 0; i < nLeafSw; i++) {
    Ptr<OFSwitch13Device> openFlowDev = of13Helper->InstallSwitch (leafNodes.Get(i), leafDevices[i]);
    openFlowDevs.push_back(openFlowDev);
  }
  for (uint32_t i = 0; i < nSpineSw; i++) {
    Ptr<OFSwitch13Device> openFlowDev = of13Helper->InstallSwitch (spineNodes.Get(i), spineDevices[i]);
    openFlowDevs.push_back(openFlowDev);
  }
  of13Helper->CreateOpenFlowChannels ();
  // OFSwitch13Helper::EnableDatapathLogs ();

  NS_LOG_INFO ("Calculating request rate");
  double requestRate = load * 1000000000 / (8 * 1024);
  NS_LOG_INFO ("Average request rate: " << requestRate << " per second");
  struct cdf_table* cdfTable = new cdf_table ();
  init_cdf (cdfTable);
  load_cdf (cdfTable, cdfFileName.c_str ());
  NS_LOG_INFO ("Initialize random seed: " << randomSeed);
  if (randomSeed == 0)
  {
      srand ((unsigned)time (NULL));
  }
  else
  {
      srand (randomSeed);
  }
  /******** Create Sender and Receiver Apps on End-hosts ********/
  NS_LOG_INFO ("Create applications");

  long flowCount = 0;
  long totalFlowSize = 0;
  
  for (uint32_t fromPodId = 0; fromPodId < nLeafSw; ++fromPodId)
  {
      InstallApplication(fromPodId, serverCount, nLeafPerSpine, hostNodes, hostToTorIfs, START_TIME, END_TIME, FLOW_LAUNCH_END_TIME, cdfTable, requestRate, TCP_SEGMENT, nHosts, flowCount, totalFlowSize);
  }
  NS_LOG_INFO ("Total flow: " << flowCount);
  NS_LOG_INFO ("Actual average flow size: " << static_cast<double> (totalFlowSize) / flowCount);
  NS_LOG_INFO ("Enabling flow monitor");
  
  /******** Set the message traces for the clients ********/
  
  /******** Enable openflow switch queue detection ********/
  
  Time initialQueueDelay = MicroSeconds(10); // Initial delay before the first execution
  Time initialSketchDelay = MilliSeconds(10);
  for(Ptr<OFSwitch13Device> openFlowDev : openFlowDevs){
    Simulator::Schedule(initialQueueDelay, &QueryAllQueLength, openFlowDev);
  }
  Simulator::Schedule(initialSketchDelay, &QuerySketch, openFlowDevs[nTors+nLeafSw+nSpineSw-1]);
  
  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.InstallAll();
  flowMonitor->CheckForLostPackets ();

  NS_LOG_INFO ("Start simulation");
  std::string dir = "singletree/openflow/" + transport_port.substr(0, transport_port.length()) + "/" + currentTime + "/";
  std::string dirToSave = "mkdir -p " + dir;
  system (dirToSave.c_str ());
  filePlotfct << dir << "/" << "FCT.dat";
  remove (filePlotfct.str ().c_str());
  std::ofstream fPlotFCT (filePlotfct.str ().c_str(), std::ios::out | std::ios::app);

  fPlotFCT << "#fct(us)" << std::endl;
  // Simulator::Schedule (progressInterval, &PrintProgress, progressInterval);
  Simulator::Stop (Seconds (END_TIME));

      
  Simulator::Run ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats ();
  
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
  {
      // Ipv4FlowClassifier::FiveTuple FiveTuple = classifier->FindFlow (i->first);
      // std::cout << "Flow " << i->first << " (" << FiveTuple.sourceAddress << " -> " << FiveTuple.destinationAddress << ")\n";
      // std::cout << "  Statr time: " << i->second.timeFirstTxPacket << "\n";
      // std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
      // std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
      // std::cout << "  TxOffered:  " << i->second.txBytes * 8.0 / 1000000 / (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
      // std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
      // std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
      // std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / 1000000/ (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
      if((i->second.timeLastRxPacket).GetMicroSeconds()-(i->second.timeFirstTxPacket).GetMicroSeconds() > 0){
      //    std::cout << "  FCT:  " << (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetMicroSeconds() << " s\n";
          fPlotFCT << std::fixed << std::setprecision (3) << (i->second.timeLastRxPacket).GetMicroSeconds()-(i->second.timeFirstTxPacket).GetMicroSeconds() << std::endl;
      }
      // if(((i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds()>max_fct) && (count<sendNum))
      // {
      // max_fct = (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds();
      // }
      // count++;
  }
  Simulator::Destroy ();
  free_cdf (cdfTable);
  NS_LOG_INFO ("Stop simulation");

  /***** Measure the actual time the simulation has taken (for reference) *****/
  auto simStop = std::chrono::high_resolution_clock::now();
  auto simTime =
      std::chrono::duration_cast<std::chrono::milliseconds>(simStop - simStart);
  double simTimeMin = (double)simTime.count() / 1e3 / 60;
  NS_LOG_UNCOND("Time taken by simulation: " << simTimeMin << " minutes");

  return 0;
}
