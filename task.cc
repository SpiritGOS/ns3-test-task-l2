#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/mobility-module.h>
#include <ns3/lte-module.h>

#include <fstream>
#include <ostream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>


// First task
void BaseScript() {
    using namespace ns3;

    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    lteHelper->SetSchedulerType("ns3::PfFfMacScheduler");
    
    NodeContainer enbNodes;
    enbNodes.Create(1);

    NodeContainer ueNodes;
    ueNodes.Create(2);

    MobilityHelper mobility;

    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(enbNodes);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(ueNodes);

    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

    lteHelper->Attach(ueDevs, enbDevs.Get(0));

    enum EpsBearer::Qci q = EpsBearer::GBR_CONV_VOICE;
    EpsBearer bearer(q);
    lteHelper->ActivateDataRadioBearer(ueDevs, bearer);

    lteHelper->EnableMacTraces();
    lteHelper->EnableRlcTraces();
    
    Simulator::Stop(Seconds(5.0));
    Simulator::Run();
    Simulator::Destroy();
}

// Second task
#define DL_RLC_TRACEFILE "DlRlcStats.txt"
#define UL_RLC_TRACEFILE "UlRlcStats.txt"

struct RawRlcData {
    double start;
    double end;
    std::string imsi;
    unsigned long long txBytes;
    unsigned long long rxBytes;
};

using RlcUserData = std::unordered_map<std::string, std::vector<RawRlcData>>;

std::istringstream & operator>>(std::istringstream & in, RawRlcData& data) {
    std::string dummy;
    in >> data.start >> data.end >> dummy >> data.imsi >> dummy >> dummy >> dummy >> data.txBytes >> dummy >> data.rxBytes;
    return in;
}

void ParseTraceFile(const std::string& filename, RlcUserData& rlcUserData) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << DL_RLC_TRACEFILE << std::endl;
    }
    std::string line;
    std::getline(file, line);
    line.clear();
    while(std::getline(file, line)) {
        std::istringstream iss(line);
        RawRlcData data;
        iss >> data;
        rlcUserData[data.imsi].push_back(data);
    }
    file.close();
}

struct AggregatedUserData {
    std::string imsi;
    double avgReceiviedThroughput;
    double avgTransferedBytes;
};

std::vector<AggregatedUserData> AggregateUserData(const RlcUserData& data) {
    std::vector<AggregatedUserData> result;
    result.reserve(data.size());
    for(const auto& pair : data) {
        AggregatedUserData userData;
        userData.imsi = pair.first;
        double totalTime = 0;
        unsigned long long totalReceiviedBytes = 0;
        unsigned long long totalTransferedBytes = 0;
        for(const auto& data : pair.second) {
            totalTime += data.end - data.start;
            totalReceiviedBytes += data.rxBytes;
            totalTransferedBytes += data.txBytes;
        }
        userData.avgReceiviedThroughput = totalReceiviedBytes / totalTime;
        userData.avgTransferedBytes = totalTransferedBytes / totalTime;
        result.push_back(userData);
    }
    return result;
}

void ProcessTraceFiles(std::ostream& out) {
    // Aggregating DL Tracefile
    RlcUserData dlRlcUserData;
    ParseTraceFile(DL_RLC_TRACEFILE, dlRlcUserData);
    auto dlAggUserData = AggregateUserData(dlRlcUserData);

    // Aggregating UL Tracefile
    RlcUserData ulRlcUserData;
    ParseTraceFile(UL_RLC_TRACEFILE, ulRlcUserData);
    auto ulAggUserData = AggregateUserData(ulRlcUserData);

    // Printing result
    std::sort(dlAggUserData.begin(), dlAggUserData.end(), [](const AggregatedUserData& lhs, const AggregatedUserData& rhs){ return lhs.imsi < rhs.imsi; });
    std::sort(ulAggUserData.begin(), ulAggUserData.end(), [](const AggregatedUserData& lhs, const AggregatedUserData& rhs){ return lhs.imsi < rhs.imsi; });

    using namespace std::literals;
    out << std::setw(4) << "IMSI"s << std::setw(32) << "Avg DL/Rx Throughput(B/s)"s << std::setw(32) <<"Avg DL/Tx Throughput(B/s)"s 
        << std::setw(32) << "Avg UL/Rx Throughput(B/s)"s << std::setw(32) <<"Avg UL/Tx Throughput(B/s)"s << std::endl;
    for (size_t user = 0; user < ulAggUserData.size(); ++user) {
        out << std::setw(4) << dlAggUserData[user].imsi << std::setw(32) << dlAggUserData[user].avgReceiviedThroughput 
            << std::setw(32) << dlAggUserData[user].avgTransferedBytes << std::setw(32) << ulAggUserData[user].avgReceiviedThroughput 
            << std::setw(32) << ulAggUserData[user].avgTransferedBytes<< std::endl;
    }
}

int main(){
    BaseScript();
    ProcessTraceFiles(std::cout);
    return 0;
}