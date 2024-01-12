/*
*该文件拓扑主要目的用来测试新增的修改接收窗口字段，和两个新的消息能不能成功传输到控制器，以及交换机能不能定时查询队列。
*                               控制器
 *                                |
 *      主机 10.0.0.1 === +-----------------+
 *          ... ...  === | OpenFlow 交换机  | === 主机 10.0.0.6
 *      主机 10.0.0.5 === +-----------------+
*/
#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/csma-module.h>
#include <ns3/internet-module.h>
#include <ns3/ofswitch13-module.h>
#include <ns3/internet-apps-module.h>
#include "ns3/applications-module.h"
#include <iostream>
#include <fstream>
#include "ns3/flow-monitor-module.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE ("OFSwitch13SimpleTopo");
class OFSwitch13LearningControllers;
//定时查询某个交换机的所有端口的所有队列长度
void QueryAllQueLength(Ptr<OFSwitch13Device> openFlowDev) {
  //获取交换机的端口数量
  size_t portSize = openFlowDev->GetSwitchPortSize();
  uint64_t dpid = openFlowDev->GetDpId();
  for(uint32_t i = 0; i < portSize; i++){
    Ptr<OFSwitch13Port> ofPort = openFlowDev->GetSwitchPort(i+1);
    Ptr<OFSwitch13Queue> ofQue = ofPort->GetPortQueue();
    uint16_t queueLength = ofQue->GetNPackets();
    NS_LOG_INFO("The Port " << i+1 << " queueLength is " << queueLength);
    uint32_t port_no = i+1;
    //判断是否大于阈值
    if(queueLength > 0){
      openFlowDev->SendQueueCongestionNotifyMessage(dpid,queueLength,port_no);
    }else{
      openFlowDev->SendQueueCongestionRecoverMessage(dpid,queueLength,port_no);
    }
    //OFSwitch13Device构造发送函数，发送到控制器
  }
  
  // Reschedule the function call
  Time delay = MicroSeconds(100000); // Set the desired time interval
  Simulator::Schedule(delay, &QueryAllQueLength, openFlowDev);
}

int
main (int argc, char *argv[])
{
  uint16_t simTime = 10;
  bool verbose = true;
  bool trace = true;
  uint32_t sendNum = 5;
  double data_mbytes = 2*1024*1024;
  // simTime：模拟时间
  // verbose：是否输出更多的信息
  // trace：开启pcap追踪，获得一些数据包的追踪
  CommandLine cmd;
  cmd.AddValue ("simTime", "Simulation time (seconds)", simTime);
  cmd.AddValue ("verbose", "Enable verbose output", verbose);
  cmd.AddValue ("trace", "Enable datapath stats and pcap traces", trace);
  cmd.Parse (argc, argv);

  if (verbose)
    {
      OFSwitch13Helper::EnableDatapathLogs ();
      // LogComponentEnable ("OFSwitch13Interface", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13Device", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13SimpleTopo", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13Port", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13Queue", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13SocketHandler", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13Controller", LOG_LEVEL_ALL);
      LogComponentEnable ("OFSwitch13TsfccController", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13Helper", LOG_LEVEL_ALL);
      // LogComponentEnable ("OFSwitch13InternalHelper", LOG_LEVEL_ALL);
    }

  // Enable checksum computations (required by OFSwitch13 module)
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  // 创建两个主机节点
  NodeContainer hosts;
  hosts.Create (6);

  // 创建交换机节点
  Ptr<Node> switchNode = CreateObject<Node> ();

  // 使用csmaHelper连接主机节点和交换机节点
  // csma是共享介质的传输协议，这里先设置信道的属性，然后在信道两端连接到NetDevice（类似于网卡），这样主机与交换机就连接上了
  CsmaHelper csmaHelper;
  csmaHelper.SetChannelAttribute ("DataRate", DataRateValue (DataRate ("1000Mbps")));
  csmaHelper.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));
  csmaHelper.SetChannelAttribute("FullDuplex", BooleanValue(true));

  NetDeviceContainer hostDevices;
  NetDeviceContainer switchPorts;
  for (size_t i = 0; i < hosts.GetN (); i++)
    {
      NodeContainer pair (hosts.Get (i), switchNode);
      NetDeviceContainer link = csmaHelper.Install (pair);
      hostDevices.Add (link.Get (0));
      switchPorts.Add (link.Get (1));
    }

  // 创建控制器节点
  Ptr<Node> controllerNode = CreateObject<Node> ();

  // 利用OFSwitch13InternalHelper配置openflow，安装上控制器和交换机，然后创建openflow信道
  Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper> ();
  Ptr<OFSwitch13Controller> controller = CreateObject<OFSwitch13LearningController> ();
  of13Helper->InstallController (controllerNode, controller);
  Ptr<OFSwitch13Device> openFlowDev = of13Helper->InstallSwitch (switchNode, switchPorts);
  of13Helper->CreateOpenFlowChannels ();


  // 给主机节点安装协议栈TCP/IP
  InternetStackHelper internet;
  internet.Install (hosts);

  // 设置IPV4地址
  Ipv4AddressHelper ipv4helpr;
  Ipv4InterfaceContainer hostIpIfaces;
  ipv4helpr.SetBase ("10.0.0.0", "255.255.255.0");
  hostIpIfaces = ipv4helpr.Assign (hostDevices);

  // 在两个主机中利用Ping程序交互
  // V4PingHelper pingHelper = V4PingHelper (hostIpIfaces.GetAddress (1));
  // pingHelper.SetAttribute ("Verbose", BooleanValue (true));
  // ApplicationContainer pingApps = pingHelper.Install (hosts.Get (0));
  // pingApps.Start (Seconds (1));
  for (uint32_t i = 0; i < hosts.GetN(); ++i) {
    V4PingHelper pingHelper(hostIpIfaces.GetAddress(5)); // 使用服务器的IP地址
    pingHelper.SetAttribute("Verbose", BooleanValue(false));

    ApplicationContainer singleHostPingApps = pingHelper.Install(hosts.Get(i));
    singleHostPingApps.Start(Seconds(0.3));

    // 将Ping应用程序添加到总的Ping应用程序容器
    // pingApps.Add(singleHostPingApps);
  }
  // 在两主机之间利用OnOffHelper开启tcp连接
  uint16_t port = 5000;
  
  // 服务器
  Address serverAddress(InetSocketAddress(hostIpIfaces.GetAddress(5), port));
  PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", serverAddress);
  ApplicationContainer sinkApps = packetSinkHelper.Install(hosts.Get(5));
  sinkApps.Start(Seconds(0.0));
  sinkApps.Stop(Seconds(10.0));

  // 客户端
  for(uint16_t i=0; i<hosts.GetN()-1; i++)
  {
    Address clientAddress(InetSocketAddress(hostIpIfaces.GetAddress(5), port));
    BulkSendHelper sourceHelper ("ns3::TcpSocketFactory", clientAddress);
    sourceHelper.SetAttribute ("MaxBytes", UintegerValue (data_mbytes/sendNum));
    ApplicationContainer sourceApp = sourceHelper.Install (hosts.Get(i));
    sourceApp.Start (Seconds (1.0));
    sourceApp.Stop (Seconds (10.0));
  }
  // 开启pcap等
  if (trace)
    {
      of13Helper->EnableOpenFlowPcap ("openflow");
      of13Helper->EnableDatapathStats ("switch-stats");
      csmaHelper.EnablePcap ("switch", switchPorts, true);
      csmaHelper.EnablePcap ("host", hostDevices);
    }
 
  // Time initialDelay = MicroSeconds(1000000); // Initial delay before the first execution
  // Simulator::Schedule(initialDelay, &QueryAllQueLength, openFlowDev);

  // Install FlowMonitor on all nodes
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();
  // Run the simulation
  Simulator::Stop (Seconds (simTime+simTime));
  Simulator::Run ();

  // Get information from FlowMonitor
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
  double max_fct=0;
  uint32_t count=0;

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple FiveTuple = classifier->FindFlow (i->first);
      std::cout << "Flow " << i->first << " (" << FiveTuple.sourceAddress << " -> " << FiveTuple.destinationAddress << ")\n";
      std::cout << "  Statr time: " << i->second.timeFirstTxPacket << "\n";
      std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
      std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
      std::cout << "  TxOffered:  " << i->second.txBytes * 8.0 / 1000000 / (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
      std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
      std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
      std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / 1000000/ (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
      std::cout << "  FCT:  " << (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " s\n";
      if(((i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds()>max_fct) && (count<sendNum))
      {
        max_fct = (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds();
      }
      count++;
    }
  double goodput = data_mbytes * 8.0 / 1000000 / max_fct;
  std::cout << "goodput: " << goodput << " Mbps" << std::endl;
  std::cout << "query FCT: " << max_fct << " s" << std::endl;
  Simulator::Destroy ();
}
