#include "dns_request.h"
#include <unistd.h>
#include <sstream>
#include <tbox/util/serializer.h>
#include <tbox/util/string.h>

namespace tbox {
namespace network {

DnsRequest::DnsRequest(event::Loop *wp_loop) :
    udp_(wp_loop)
{
    using namespace std::placeholders;
    udp_.setRecvCallback(std::bind(&DnsRequest::onUdpRecv, this, _1, _2, _3));
    udp_.enable();
}

DnsRequest::~DnsRequest() {
    udp_.disable();
}

namespace {

constexpr uint16_t DNS_TYPE_A = 0x0001;
constexpr uint16_t DNS_TYPE_CNAME = 0x0005;

void AppendDomain(util::Serializer &dump, const std::string domain) {
    std::vector<std::string> str_vec;
    util::string::Split(domain, ".", str_vec);

    for (auto &seg : str_vec) {
        dump << uint8_t(seg.length());
        dump.append(seg.data(), seg.length());
    }
    dump << uint8_t(0);
}

std::string FetchDomain(util::Deserializer &parser) {
    std::ostringstream oss;
    bool first = true;
    for (;;) {
        uint8_t len = 0;
        parser >> len;
        if (len == 0)
            break;

        if (!first)
            oss << '.';
        first = false;

        if ((len & 0xc0) == 0xc0) {
            uint8_t offset_low = 0;
            parser >> offset_low;
            uint16_t offset = (len & 0x3f) << 8 | offset_low;
            util::Deserializer sub_parser(parser);
            sub_parser.set_pos(offset);
            oss << FetchDomain(sub_parser);
            break;
        } else {
            char str[len + 1] = { 0 };
            parser.fetch(str, len);
            oss << str;
        }
    }
    return oss.str();
}

}

bool DnsRequest::request(const std::string &domain) {
    std::vector<uint8_t> send_buff;
    util::Serializer dump(send_buff);
    
    uint16_t id = ::getpid();
    uint16_t flags = 0x0100;
    uint16_t qd_count = 1;
    uint16_t an_count = 0;
    uint16_t ns_count = 0;
    uint16_t ar_count = 0;
    uint16_t dns_type = 1;
    uint16_t dns_class = 1;

    dump << id
         << flags
         << qd_count
         << an_count
         << ns_count
         << ar_count;

    AppendDomain(dump, domain);

    dump << dns_type
         << dns_class;

    SockAddr dns_srv(IPAddress("114.114.114.114"), 53);
    std::string hex_str = util::string::RawDataToHexStr(send_buff.data(), send_buff.size());
    LogInfo("send to %s : %s", dns_srv.toString().c_str(), hex_str.c_str());

    udp_.send(send_buff.data(), send_buff.size(), dns_srv);
    return true;
}

void DnsRequest::onUdpRecv(const void *data_ptr, size_t data_size, const SockAddr &from) {
    std::string hex_str = util::string::RawDataToHexStr(data_ptr, data_size);
    LogInfo("recv from %s : %s", from.toString().c_str(), hex_str.c_str());
    util::Deserializer parser(data_ptr, data_size);

    uint16_t id, flags;
    uint16_t qd_count, an_count, ns_count, ar_count;

    parser >> id >> flags >> qd_count >> an_count >> ns_count >> ar_count;
    LogDbg("id:%d, flags:%04x, qd_count:%d, an_count:%d, ns_count:%d, ar_count:%d",
           id, flags, qd_count, an_count, an_count, ns_count, ar_count);

    //! 解析Question字段
    LogDbg("=== questions ===");
    for (uint16_t i = 0; i < qd_count; ++i) {
        std::string domain = FetchDomain(parser);
        LogInfo("domin:%s", domain.c_str());

        uint16_t dns_type, dns_class;
        parser >> dns_type >> dns_class;
        LogDbg("dns_type:%d, dns_class:%d", dns_type, dns_class);
    }

    LogDbg("=== answers ===");
    for (uint16_t i = 0; i < an_count; ++i) {
        std::string domain = FetchDomain(parser);
        LogDbg("domain:%s", domain.c_str());
        uint16_t an_type, an_class, an_len;
        uint32_t an_ttl;
        parser >> an_type >> an_class >> an_ttl >> an_len;

        LogDbg("type:%d, class:%d, ttl:%d, len:%d", an_type, an_class, an_ttl, an_len);
        if (an_type == DNS_TYPE_A) {
            uint32_t ip_value;
            auto old_endian = parser.setEndian(util::Endian::kLittle);
            parser >> ip_value;
            parser.setEndian(old_endian);
            IPAddress ip(ip_value);
            LogDbg("A:%08x, %s", ip_value, ip.toString().c_str());

        } else if (an_type == DNS_TYPE_CNAME) {
            std::string domain = FetchDomain(parser);
            LogDbg("CNAME:%s", domain.c_str());

        } else {
            LogNotice("unknow type:%d", an_type);
            parser.skip(an_len);
        }
        LogDbg("-------");
    }
}

}
}