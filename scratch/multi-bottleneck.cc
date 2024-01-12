/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017-20 NITK Surathkal
 * Copyright (c) 2020 Tom Henderson (better alignment with experiment)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Shravya K.S. <shravya.ks0@gmail.com>
 *          Apoorva Bhargava <apoorvabhargava13@gmail.com>
 *          Shikha Bakshi <shikhabakshi912@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 *          Tom Henderson <tomh@tomh.org>
 */

// The network topology used in this example is based on Fig. 17 described in
// Mohammad Alizadeh, Albert Greenberg, David A. Maltz, Jitendra Padhye,
// Parveen Patel, Balaji Prabhakar, Sudipta Sengupta, and Murari Sridharan.
// "Data Center TCP (DCTCP)." In ACM SIGCOMM Computer Communication Review,
// Vol. 40, No. 4, pp. 63-74. ACM, 2010.

// The topology is roughly as follows
//
//  S1         S3
//  |           |  (1 Gbps)
//  T1 ------- T2 -- R1
//  |           |  (1 Gbps)
//  S2         R2
//
// The link between switch T1 and T2 is 10 Gbps.  All other
// links are 1 Gbps.  In the SIGCOMM paper, there is a Scorpion switch
// between T1 and T2, but it doesn't contribute another bottleneck.
//
// S1 and S3 each have 10 senders sending to receiver R1 (20 total)
// S2 (20 senders) sends traffic to R2 (20 receivers)
//
// This sets up two bottlenecks: 1) T1 -> T2 interface (30 senders
// using the 10 Gbps link) and 2) T2 -> R1 (20 senders using 1 Gbps link)
//
// RED queues configured for ECN marking are used at the bottlenecks.
//
// Figure 17 published results are that each sender in S1 gets 46 Mbps
// and each in S3 gets 54 Mbps, while each S2 sender gets 475 Mbps, and
// that these are within 10% of their fair-share throughputs (Jain index
// of 0.99).
//
// This program runs the program by default for five seconds.  The first
// second is devoted to flow startup (all 40 TCP flows are stagger started
// during this period).  There is a three second convergence time where
// no measurement data is taken, and then there is a one second measurement
// interval to gather raw throughput for each flow.  These time intervals
// can be changed at the command line.
//
// The program outputs six files.  The first three:
// * dctcp-example-s1-r1-throughput.dat
// * dctcp-example-s2-r2-throughput.dat
// * dctcp-example-s3-r1-throughput.dat
// provide per-flow throughputs (in Mb/s) for each of the forty flows, summed
// over the measurement window.  The fourth file,
// * dctcp-example-fairness.dat
// provides average throughputs for the three flow paths, and computes
// Jain's fairness index for each flow group (i.e. across each group of
// 10, 20, and 10 flows).  It also sums the throughputs across each bottleneck.
// The fifth and sixth:
// * dctcp-example-t1-length.dat
// * dctcp-example-t2-length.dat
// report on the bottleneck queue length (in packets and microseconds
// of delay) at 10 ms intervals during the measurement window.
//
// By default, the throughput averages are 23 Mbps for S1 senders, 471 Mbps
// for S2 senders, and 74 Mbps for S3 senders, and the Jain index is greater
// than 0.99 for each group of flows.  The average queue delay is about 1ms
// for the T2->R2 bottleneck, and about 200us for the T1->T2 bottleneck.
//
// The RED parameters (min_th and max_th) are set to the same values as
// reported in the paper, but we observed that throughput distributions
// and queue delays are very sensitive to these parameters, as was also
// observed in the paper; it is likely that the paper's throughput results
// could be achieved by further tuning of the RED parameters.  However,
// the default results show that DCTCP is able to achieve high link
// utilization and low queueing delay and fairness across competing flows
// sharing the same path.

#include <iostream>
#include <iomanip>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE ("dctcp-example");

std::stringstream filePlotQueue1;
std::stringstream filePlotQueue2;
std::ofstream rxS1R1Throughput;
std::ofstream rxS2R2Throughput;
std::ofstream rxS3R1Throughput;
std::ofstream fairnessIndex;
std::ofstream t1QueueLength;
std::ofstream t2QueueLength;
std::vector<uint64_t> rxS1R1Bytes;
std::vector<uint64_t> rxS2R2Bytes;
std::vector<uint64_t> rxS3R1Bytes;

size_t S1N = 10;
size_t S2N = 20;
size_t S3N = 20;
size_t R2N = 20;

void
PrintProgress (Time interval)
{
  std::cout << "Progress to " << std::fixed << std::setprecision (2) << Simulator::Now ().GetSeconds () << " seconds simulation time" << std::endl;
  Simulator::Schedule (interval, &PrintProgress, interval);
}

void
TraceS1R1Sink (std::size_t index, Ptr<const Packet> p, const Address& a)
{
  rxS1R1Bytes[index] += p->GetSize ();
}

void
TraceS2R2Sink (std::size_t index, Ptr<const Packet> p, const Address& a)
{
  rxS2R2Bytes[index] += p->GetSize ();
}

void
TraceS3R1Sink (std::size_t index, Ptr<const Packet> p, const Address& a)
{
  rxS3R1Bytes[index] += p->GetSize ();
}

void
InitializeCounters (void)
{
  for (std::size_t i = 0; i < S1N; i++)
    {
      rxS1R1Bytes[i] = 0;
    }
  for (std::size_t i = 0; i < S2N; i++)
    {
      rxS2R2Bytes[i] = 0;
    }
  for (std::size_t i = 0; i < S3N; i++)
    {
      rxS3R1Bytes[i] = 0;
    }
}

void
PrintThroughput (Time measurementWindow)
{
  for (std::size_t i = 0; i < S1N; i++)
    {
      rxS1R1Throughput << measurementWindow.GetSeconds () << "s " << i << " " << (rxS1R1Bytes[i] * 8) / (measurementWindow.GetSeconds ()) / 1e6 << std::endl;
    }
  for (std::size_t i = 0; i < S2N; i++)
    {
      rxS2R2Throughput << Simulator::Now ().GetSeconds () << "s " << i << " " << (rxS2R2Bytes[i] * 8) / (measurementWindow.GetSeconds ()) / 1e6 << std::endl;
    }
  for (std::size_t i = 0; i < S3N; i++)
    {
      rxS3R1Throughput << Simulator::Now ().GetSeconds () << "s " << i << " " << (rxS3R1Bytes[i] * 8) / (measurementWindow.GetSeconds ()) / 1e6 << std::endl;
    }
}

// Jain's fairness index:  https://en.wikipedia.org/wiki/Fairness_measure
void
PrintFairness (Time measurementWindow)
{
  double average = 0;
  uint64_t sumSquares = 0;
  uint64_t sum = 0;
  double fairness = 0;
  for (std::size_t i = 0; i < S1N; i++)
    {
      sum += rxS1R1Bytes[i];
      sumSquares += (rxS1R1Bytes[i] * rxS1R1Bytes[i]);
    }
  average = ((sum / S1N) * 8 / measurementWindow.GetSeconds ()) / 1e6;
  fairness = static_cast<double> (sum * sum) / (S1N * sumSquares);
  fairnessIndex << "Average throughput for S1-R1 flows: "
                << std::fixed << std::setprecision (2) << average << " Mbps; fairness: "
                << std::fixed << std::setprecision (3) << fairness << std::endl;
  average = 0;
  sumSquares = 0;
  sum = 0;
  fairness = 0;
  for (std::size_t i = 0; i < S2N; i++)
    {
      sum += rxS2R2Bytes[i];
      sumSquares += (rxS2R2Bytes[i] * rxS2R2Bytes[i]);
    }
  average = ((sum / S2N) * 8 / measurementWindow.GetSeconds ()) / 1e6;
  fairness = static_cast<double> (sum * sum) / (S2N * sumSquares);
  fairnessIndex << "Average throughput for S2-R2 flows: "
                << std::fixed << std::setprecision (2) << average << " Mbps; fairness: "
                << std::fixed << std::setprecision (3) << fairness << std::endl;
  average = 0;
  sumSquares = 0;
  sum = 0;
  fairness = 0;
  for (std::size_t i = 0; i < S3N; i++)
    {
      sum += rxS3R1Bytes[i];
      sumSquares += (rxS3R1Bytes[i] * rxS3R1Bytes[i]);
    }
  average = ((sum / S3N) * 8 / measurementWindow.GetSeconds ()) / 1e6;
  fairness = static_cast<double> (sum * sum) / (S3N * sumSquares);
  fairnessIndex << "Average throughput for S3-R1 flows: "
                << std::fixed << std::setprecision (2) << average << " Mbps; fairness: "
                << std::fixed << std::setprecision (3) << fairness << std::endl;
  sum = 0;
  for (std::size_t i = 0; i < S1N; i++)
    {
      sum += rxS1R1Bytes[i];
    }
  for (std::size_t i = 0; i < S2N; i++)
    {
      sum += rxS2R2Bytes[i];
    }
  fairnessIndex << "Aggregate user-level throughput for flows through T1: " << static_cast<double> (sum * 8) / 1e9 / measurementWindow.GetSeconds() << " Gbps" << std::endl;
  sum = 0;
  for (std::size_t i = 0; i < S3N; i++)
    {
      sum += rxS3R1Bytes[i];
    }
  for (std::size_t i = 0; i < S1N; i++)
    {
      sum += rxS1R1Bytes[i];
    }
  fairnessIndex << "Aggregate user-level throughput for flows to R1: " << static_cast<double> (sum * 8) / 1e9 / measurementWindow.GetSeconds() << " Gbps" << std::endl;
}

void
CheckT1QueueSize (Ptr<QueueDisc> queue)
{
  // 1500 byte packets
  uint32_t qSize = queue->GetNPackets ();
  Time backlog = Seconds (static_cast<double> (qSize * 1500 * 8) / 1e11); // 100 Gb/s
  // report size in units of packets and ms
  t1QueueLength << std::fixed << std::setprecision (3) << Simulator::Now ().GetSeconds () << " " << qSize << " " << backlog.GetMicroSeconds () << std::endl;
  // check queue size every 1/100 of a second
  Simulator::Schedule (MilliSeconds (1), &CheckT1QueueSize, queue);
}

void
CheckT2QueueSize (Ptr<QueueDisc> queue)
{
  uint32_t qSize = queue->GetNPackets ();
  Time backlog = Seconds (static_cast<double> (qSize * 1500 * 8) / 1e10); // 10 Gb/s
  // report size in units of packets and ms
  t2QueueLength << std::fixed << std::setprecision (3) << Simulator::Now ().GetSeconds () << " " << qSize << " " << backlog.GetMicroSeconds () << std::endl;
  // check queue size every 1/100 of a second
  Simulator::Schedule (MilliSeconds (1), &CheckT2QueueSize, queue);
}

void
A3DCTCongestionOpsTrace (uint64_t flowSize, Ptr<TcpCongestionOps> ops)
{
  ops->SetAttribute("TotalBytes", UintegerValue (flowSize));
}


int main (int argc, char *argv[])
{
// LogComponentEnable("TcpA3dct", LOG_LEVEL_INFO);
  std::string outputFilePath = ".";
  std::string tcpTypeId = "TcpDctcp";//TcpSwift TcpDctcp TcpDcvegas TcpDstcp TcpEar TcpNewReno TcpCubic
  Time flowStartupWindow = Seconds (0.5);
  Time convergenceTime = Seconds (1);
  Time measurementWindow = Seconds (0.5);
  bool enableSwitchEcn = true;
  Time progressInterval = MilliSeconds (10);
  bool Synchronous = true;
  bool tracing = true;

  // Create directory information
  time_t rawtime;
  struct tm * timeinfo;
  char buffer[80];
  time (&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer,sizeof(buffer),"%d-%m-%Y-%I-%M-%S",timeinfo);
  std::string currentTime (buffer);

  CommandLine cmd (__FILE__);
  cmd.AddValue ("tcpTypeId", "ns-3 TCP TypeId, TcpDctcp, TcpDcvegas or TcpDstcp", tcpTypeId);
  cmd.AddValue ("flowStartupWindow", "startup time window (TCP staggered starts)", flowStartupWindow);
  cmd.AddValue ("convergenceTime", "convergence time", convergenceTime);
  cmd.AddValue ("measurementWindow", "measurement window", measurementWindow);
  cmd.AddValue ("enableSwitchEcn", "enable ECN at switches", enableSwitchEcn);
  cmd.AddValue ("syn", "syn or not", Synchronous);
  cmd.AddValue ("s1n", "s1 number", S1N);
  cmd.AddValue ("s2n", "s2 number", S2N);
  cmd.AddValue ("s3n", "s3 number", S3N);
  cmd.AddValue ("r2n", "r2 number", R2N);
  cmd.Parse (argc, argv);
  LogComponentEnable("dctcp-example", LOG_LEVEL_INFO);

  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + tcpTypeId));

  Time startTime = Seconds (0);
  Time stopTime = flowStartupWindow + convergenceTime + measurementWindow;

  Time clientStartTime = startTime;

  rxS1R1Bytes.reserve (S1N);
  rxS2R2Bytes.reserve (S2N);
  rxS3R1Bytes.reserve (S3N);

  NS_LOG_INFO ("create nodes");
  NodeContainer S1, S2, S3, R2;
  Ptr<Node> T1 = CreateObject<Node> ();
  Ptr<Node> T2 = CreateObject<Node> ();
  Ptr<Node> R1 = CreateObject<Node> ();
  S1.Create (S1N);//10
  S2.Create (S2N);//20
  S3.Create (S3N);//10
  R2.Create (R2N);//20

  NS_LOG_INFO ("Configure " << tcpTypeId <<  " Socket");
  Config::SetDefault ("ns3::TcpSocketBase::Timestamp", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1460));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (false));

  // Set default parameters for RED queue disc
  Config::SetDefault ("ns3::RedQueueDisc::UseEcn", BooleanValue (enableSwitchEcn));
  // ARED may be used but the queueing delays will increase; it is disabled
  // here because the SIGCOMM paper did not mention it
  // Config::SetDefault ("ns3::RedQueueDisc::ARED", BooleanValue (true));
  // Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (true));
  Config::SetDefault ("ns3::RedQueueDisc::UseHardDrop", BooleanValue (false));
  Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (1500));
  // Triumph and Scorpion switches used in DCTCP Paper have 4 MB of buffer
  // If every packet is 1500 bytes, 2666 packets can be stored in 4 MB
  Config::SetDefault ("ns3::RedQueueDisc::MaxSize", QueueSizeValue (QueueSize ("2666p")));
  // DCTCP tracks instantaneous queue length only; so set QW = 1
  Config::SetDefault ("ns3::RedQueueDisc::QW", DoubleValue (1));
  Config::SetDefault ("ns3::RedQueueDisc::MinTh", DoubleValue (20));
  Config::SetDefault ("ns3::RedQueueDisc::MaxTh", DoubleValue (60));
  // For fifo
  Config::SetDefault("ns3::FifoQueueDisc::MaxSize", QueueSizeValue (QueueSize ("2666p")));
  // for dcvegas
  Config::SetDefault ("ns3::TcpDcvegas::DcvegasNqK", UintegerValue (21));//1Gbps--6 10Gbps--21

  PointToPointHelper pointToPointSR;
  pointToPointSR.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
  pointToPointSR.SetChannelAttribute ("Delay", StringValue ("10us"));

  PointToPointHelper pointToPointT;
  pointToPointT.SetDeviceAttribute ("DataRate", StringValue ("100Gbps"));
  pointToPointT.SetChannelAttribute ("Delay", StringValue ("10us"));


  // Create a total of 62 links.
  std::vector<NetDeviceContainer> S1T1;
  S1T1.reserve (S1N);
  std::vector<NetDeviceContainer> S2T1;
  S2T1.reserve (S2N);
  std::vector<NetDeviceContainer> S3T2;
  S3T2.reserve (S3N);
  std::vector<NetDeviceContainer> R2T2;
  R2T2.reserve (R2N);
  NetDeviceContainer T1T2 = pointToPointT.Install (T1, T2);
  NetDeviceContainer R1T2 = pointToPointSR.Install (R1, T2);

  for (std::size_t i = 0; i < S1N; i++)
    {
      Ptr<Node> n = S1.Get (i);
      S1T1.push_back (pointToPointSR.Install (n, T1));
    }
  for (std::size_t i = 0; i < S2N; i++)
    {
      Ptr<Node> n = S2.Get (i);
      S2T1.push_back (pointToPointSR.Install (n, T1));
    }
  for (std::size_t i = 0; i < S3N; i++)
    {
      Ptr<Node> n = S3.Get (i);
      S3T2.push_back (pointToPointSR.Install (n, T2));
    }
  for (std::size_t i = 0; i < R2N; i++)
    {
      Ptr<Node> n = R2.Get (i);
      R2T2.push_back (pointToPointSR.Install (n, T2));
    }

  InternetStackHelper stack;
  stack.InstallAll ();

  TrafficControlHelper tchRed10;
  // MinTh = 50, MaxTh = 150 recommended in ACM SIGCOMM 2010 DCTCP Paper
  // This yields a target (MinTh) queue depth of 60us at 10 Gb/s
  if(tcpTypeId == "TcpDctcp" || tcpTypeId == "TcpL2dct" || tcpTypeId == "TcpA3dct")
    {
      NS_LOG_INFO ("Conf RED for 100G link");
      tchRed10.SetRootQueueDisc ("ns3::RedQueueDisc",
                                 "LinkBandwidth", StringValue ("100Gbps"),
                                 "LinkDelay", StringValue ("10us"),
                                 "MinTh", DoubleValue (100),
                                 "MaxTh", DoubleValue (100));
    }
  else
    {
      NS_LOG_INFO ("Conf FIFO for 100G link");
      tchRed10.SetRootQueueDisc ("ns3::FifoQueueDisc");
    }
  QueueDiscContainer queueDiscs1 = tchRed10.Install (T1T2);

  TrafficControlHelper tchRed1;
  // MinTh = 20, MaxTh = 60 recommended in ACM SIGCOMM 2010 DCTCP Paper
  // This yields a target queue depth of 250us at 1 Gb/s
  if(tcpTypeId == "TcpDctcp" || tcpTypeId == "TcpL2dct" || tcpTypeId == "TcpA3dct")
    {
      NS_LOG_INFO ("Conf RED for 10G link");
      tchRed1.SetRootQueueDisc ("ns3::RedQueueDisc",
                                "LinkBandwidth", StringValue ("10Gbps"),
                                "LinkDelay", StringValue ("10us"),
                                "MinTh", DoubleValue (40),
                                "MaxTh", DoubleValue (40));
    }
  else
    {
      NS_LOG_INFO ("Conf FIFO for 10G link");
      tchRed1.SetRootQueueDisc ("ns3::FifoQueueDisc");
    }
  QueueDiscContainer queueDiscs2 = tchRed1.Install (R1T2.Get (1));
  for (std::size_t i = 0; i < S1N; i++)
    {
      tchRed1.Install (S1T1[i].Get (1));
    }
  for (std::size_t i = 0; i < S2N; i++)
    {
      tchRed1.Install (S2T1[i].Get (1));
    }
  for (std::size_t i = 0; i < S3N; i++)
    {
      tchRed1.Install (S3T2[i].Get (1));
    }
  for (std::size_t i = 0; i < R2N; i++)
    {
      tchRed1.Install (R2T2[i].Get (1));
    }
  NS_LOG_INFO ("Conf IP Address");
  Ipv4AddressHelper address;
  std::vector<Ipv4InterfaceContainer> ipS1T1;
  ipS1T1.reserve (S1N);
  std::vector<Ipv4InterfaceContainer> ipS2T1;
  ipS2T1.reserve (S2N);
  std::vector<Ipv4InterfaceContainer> ipS3T2;
  ipS3T2.reserve (S3N);
  std::vector<Ipv4InterfaceContainer> ipR2T2;
  ipR2T2.reserve (R2N);
  address.SetBase ("172.16.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ipT1T2 = address.Assign (T1T2);
  address.SetBase ("192.168.0.0", "255.255.255.0");
  Ipv4InterfaceContainer ipR1T2 = address.Assign (R1T2);
  address.SetBase ("10.1.1.0", "255.255.255.0");
  for (std::size_t i = 0; i < S1N; i++)
    {
      ipS1T1.push_back (address.Assign (S1T1[i]));
      address.NewNetwork ();
    }
  address.SetBase ("10.2.1.0", "255.255.255.0");
  for (std::size_t i = 0; i < S2N; i++)
    {
      ipS2T1.push_back (address.Assign (S2T1[i]));
      address.NewNetwork ();
    }
  address.SetBase ("10.3.1.0", "255.255.255.0");
  for (std::size_t i = 0; i < S3N; i++)
    {
      ipS3T2.push_back (address.Assign (S3T2[i]));
      address.NewNetwork ();
    }
  address.SetBase ("10.4.1.0", "255.255.255.0");
  for (std::size_t i = 0; i < R2N; i++)
    {
      ipR2T2.push_back (address.Assign (R2T2[i]));
      address.NewNetwork ();
    }

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  NS_LOG_INFO ("Build connections");
  // Each sender in S2 sends to a receiver in R2
  std::vector<Ptr<PacketSink> > r2Sinks;
  r2Sinks.reserve (S2N);
  for (std::size_t i = 0; i < S2N; i++)
    {
      uint16_t port = 50000 + i;
      Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
      PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
      ApplicationContainer sinkApp = sinkHelper.Install (R2.Get (i));
      Ptr<PacketSink> packetSink = sinkApp.Get (0)->GetObject<PacketSink> ();
      r2Sinks.push_back (packetSink);
      sinkApp.Start (startTime);
      sinkApp.Stop (stopTime);

      // app
      ApplicationContainer clientApps1;
      InetSocketAddress remoteAddress (ipR2T2[i].GetAddress (0), port);
      BulkSendHelper clientHelper1 ("ns3::TcpSocketFactory", remoteAddress);
      clientHelper1.SetAttribute("SendSize", UintegerValue(1000));
      clientHelper1.SetAttribute ("MaxBytes", UintegerValue (4294967295));

      clientApps1.Add (clientHelper1.Install (S2.Get (i)));
      if (Synchronous)
        {
          clientApps1.Start (clientStartTime);
        }
      else
        {
          clientApps1.Start (i * flowStartupWindow / S2N + clientStartTime);
        }
      clientApps1.Stop (stopTime);
      if(tcpTypeId == "TcpA3dct")
        {
            for(auto it=clientApps1.Begin(); it!=clientApps1.End(); it++)
                (*it)->TraceConnectWithoutContext ("CongestionOps", MakeBoundCallback (&A3DCTCongestionOpsTrace, 4294967295));
        }
    }
  // Each sender in S1 and S3 sends to R1
  std::vector<Ptr<PacketSink> > s1r1Sinks;
  std::vector<Ptr<PacketSink> > s3r1Sinks;
  s1r1Sinks.reserve (S1N);
  s3r1Sinks.reserve (S3N);
  for (std::size_t i = 0; i < S1N+S3N; i++)
    {
      uint16_t port = 50000 + i;
      Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
      PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
      ApplicationContainer sinkApp = sinkHelper.Install (R1);
      Ptr<PacketSink> packetSink = sinkApp.Get (0)->GetObject<PacketSink> ();
      if (i < S1N)
        {
          s1r1Sinks.push_back (packetSink);
        }
      else
        {
          s3r1Sinks.push_back (packetSink);
        }
      sinkApp.Start (startTime);
      sinkApp.Stop (stopTime);

    //   OnOffHelper clientHelper1 ("ns3::TcpSocketFactory", Address ());
    //   clientHelper1.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    //   clientHelper1.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
    //   clientHelper1.SetAttribute ("DataRate", DataRateValue (DataRate ("1Gbps")));
    //   clientHelper1.SetAttribute ("PacketSize", UintegerValue (1000));
    

      ApplicationContainer clientApps1;
      InetSocketAddress remoteAddress (ipR1T2.GetAddress (0), port);
      BulkSendHelper clientHelper1 ("ns3::TcpSocketFactory", remoteAddress);
      clientHelper1.SetAttribute("SendSize", UintegerValue(1000));
      clientHelper1.SetAttribute ("MaxBytes", UintegerValue (4294967295));

      if (i < S1N)
        {
          clientApps1.Add (clientHelper1.Install (S1.Get (i)));
          if (Synchronous)
            {
              clientApps1.Start (clientStartTime);
            }
          else 
            {
              clientApps1.Start (i * flowStartupWindow / S1N + clientStartTime);
            }          
        }
      else
        {
          clientApps1.Add (clientHelper1.Install (S3.Get (i - S1N)));
          if (Synchronous)
            {
              clientApps1.Start (clientStartTime);
            }
          else
            {
              clientApps1.Start ((i - S1N) * flowStartupWindow / S1N + clientStartTime );
            }          
        }
      clientApps1.Stop (stopTime);
      if(tcpTypeId == "TcpA3dct")
        {
            for(auto it=clientApps1.Begin(); it!=clientApps1.End(); it++)
                (*it)->TraceConnectWithoutContext ("CongestionOps", MakeBoundCallback (&A3DCTCongestionOpsTrace, 4294967295));
        }
    }

  NS_LOG_INFO ("Open files for Collecting Data");
  // Collect data
  std::string dir = "multi_bottleneck/" + tcpTypeId.substr(0, tcpTypeId.length()) + "/" + currentTime + "/";
  std::cout << "Data directory:" << dir << std::endl;
  if (tracing)
  {
	  std::string dirToSave = "mkdir -p " + dir;
	  system (dirToSave.c_str ());

    rxS1R1Throughput.open (dir+"example-s1-r1-throughput.dat", std::ios::out);
    rxS1R1Throughput << "#Time(s) flow thruput(Mb/s)" << std::endl;
    rxS2R2Throughput.open (dir+"example-s2-r2-throughput.dat", std::ios::out);
    rxS2R2Throughput << "#Time(s) flow thruput(Mb/s)" << std::endl;
    rxS3R1Throughput.open (dir+"example-s3-r1-throughput.dat", std::ios::out);
    rxS3R1Throughput << "#Time(s) flow thruput(Mb/s)" << std::endl;
    fairnessIndex.open (dir+"example-fairness.dat", std::ios::out);
    t1QueueLength.open (dir+"example-t1-length.dat", std::ios::out);
    t1QueueLength << "#Time(s) qlen(pkts) qlen(us)" << std::endl;
    t2QueueLength.open (dir+"example-t2-length.dat", std::ios::out);
    t2QueueLength << "#Time(s) qlen(pkts) qlen(us)" << std::endl;
    for (std::size_t i = 0; i < S1N; i++)
      {
        s1r1Sinks[i]->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&TraceS1R1Sink, i));
      }
    for (std::size_t i = 0; i < S2N; i++)
      {
        r2Sinks[i]->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&TraceS2R2Sink, i));
      }
    for (std::size_t i = 0; i < S3N; i++)
      {
        s3r1Sinks[i]->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&TraceS3R1Sink, i));
      }
    Simulator::Schedule (flowStartupWindow + convergenceTime, &InitializeCounters);
    // Simulator::Schedule (flowStartupWindow + convergenceTime + measurementWindow, &PrintThroughput, measurementWindow);
    // Simulator::Schedule (flowStartupWindow + convergenceTime + measurementWindow, &PrintFairness, measurementWindow);
    Simulator::Schedule (progressInterval, &PrintProgress, progressInterval);
    Simulator::Schedule (flowStartupWindow + convergenceTime, &CheckT1QueueSize, queueDiscs1.Get (0));
    Simulator::Schedule (flowStartupWindow + convergenceTime, &CheckT2QueueSize, queueDiscs2.Get (0));
  }
  Simulator::Stop (stopTime + TimeStep (1));

  Simulator::Run ();

  rxS1R1Throughput.close ();
  rxS2R2Throughput.close ();
  rxS3R1Throughput.close ();
  fairnessIndex.close ();
  t1QueueLength.close ();
  t2QueueLength.close ();
  Simulator::Destroy ();
  return 0;
}
