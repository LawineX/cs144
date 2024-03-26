// #include <iostream>

// #include "arp_message.hh"
// #include "exception.hh"
// #include "network_interface.hh"

// using namespace std;

// //! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
// //! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
// NetworkInterface::NetworkInterface( string_view name,
//                                     shared_ptr<OutputPort> port,
//                                     const EthernetAddress& ethernet_address,
//                                     const Address& ip_address )
//   : name_( name )
//   , port_( notnull( "OutputPort", move( port ) ) )
//   , ethernet_address_( ethernet_address )
//   , ip_address_( ip_address )
// {
//   cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
//        << ip_address.ip() << "\n";
// }

// //! \param[in] dgram the IPv4 datagram to be sent
// //! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
// //! may also be another host if directly connected to the same network as the destination) Note: the Address type
// //! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
// void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
// {
//   auto ip = next_hop.ipv4_numeric();
//   auto it = arp_hash_map.find( ip );
//   if ( it == arp_hash_map.end() ) {
//     if ( same_arp_pair.first==ip&& same_arp_pair.second  ) {
//       unarp_queue[ip].push( { ip, dgram } );
//       return;
//     } else {
//       EthernetHeader arp_header = { ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP };
//       ARPMessage arp_message;
//       arp_message.sender_ethernet_address = ethernet_address_;
//       arp_message.sender_ip_address = ip_address_.ipv4_numeric();
//       arp_message.target_ip_address = next_hop.ipv4_numeric();
//       arp_message.opcode = ARPMessage::OPCODE_REQUEST;
//       transmit( { arp_header, serialize( arp_message ) } );
//       same_arp_pair.second = 5000;
//       same_arp_pair.first = ip;
//       unarp_queue[ip].push( { ip, dgram } );
//     }

//   } else {
//     EthernetHeader send_header = { it->second.first, ethernet_address_, EthernetHeader::TYPE_IPv4 };
//     transmit( { send_header, serialize( dgram ) } );
//   }
// }

// //! \param[in] frame the incoming Ethernet frame
// void NetworkInterface::recv_frame( const EthernetFrame& frame )
// {
//   EthernetHeader frame_header = frame.header;
//   EthernetHeader sender_header = { frame_header.src, ethernet_address_, 0 };

//   if ( frame_header.type == EthernetHeader::TYPE_ARP
//        && ( ( frame_header.dst == ethernet_address_ ) || ( frame_header.dst == ETHERNET_BROADCAST ) ) ) {
//     ARPMessage arp_msg;
//     if ( parse( arp_msg, frame.payload ) ) {
//       auto ip = arp_msg.sender_ip_address;
//       arp_hash_map[ip] = { arp_msg.sender_ethernet_address, 30000 };
//       if ( arp_msg.opcode == ARPMessage::OPCODE_REQUEST
//            && arp_msg.target_ip_address == ip_address_.ipv4_numeric() ) {
//         ARPMessage sender_arp_msg;
//         sender_arp_msg.opcode = ARPMessage::OPCODE_REPLY;
//         sender_arp_msg.sender_ethernet_address = ethernet_address_;
//         sender_arp_msg.sender_ip_address = ip_address_.ipv4_numeric();
//         sender_arp_msg.target_ethernet_address = arp_msg.sender_ethernet_address;
//         sender_arp_msg.target_ip_address = arp_msg.sender_ip_address;
//         sender_header.type = EthernetHeader::TYPE_ARP;
//         transmit( { sender_header, serialize( sender_arp_msg ) } );
//       }

//       while ( !unarp_queue[ip].empty() && arp_hash_map.find( unarp_queue[ip].front().ipv4 ) != arp_hash_map.end() ) {
//         EthernetHeader resend_header
//           = { arp_hash_map[unarp_queue[ip].front().ipv4].first, ethernet_address_, EthernetHeader::TYPE_IPv4 };
//         transmit( { resend_header, serialize( unarp_queue[ip].front().dgram ) } );
//         unarp_queue[ip].pop();
//       }
//     }

//   } else if ( frame_header.type == EthernetHeader::TYPE_IPv4 && frame_header.dst == ethernet_address_ ) {
//     InternetDatagram data_msg;
//     if ( parse( data_msg, frame.payload ) ) {
//       datagrams_received_.emplace(move(data_msg ));
//     }
//   }
// }

// //! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
// void NetworkInterface::tick( const size_t ms_since_last_tick )
// {
//   same_arp_pair.second = same_arp_pair.second >= ms_since_last_tick ? same_arp_pair.second - ms_since_last_tick : 0;
//   if(same_arp_pair.second==0){
//     same_arp_pair.first = 0;
//   }
//   vector<uint32_t> keys_to_erase;
//   for ( auto& [ipv4_, val] : arp_hash_map ) {
//     auto& [eth_, time_] = val;
//     if ( time_ <= ms_since_last_tick ) {
//       keys_to_erase.push_back( ipv4_ );
//     } else {
//       time_ -= ms_since_last_tick;
//     }
//   }
//   for ( auto& key : keys_to_erase ) {
//     arp_hash_map.erase( key );
//   }
// }


 
#include "network_interface.hh"
#include "address.hh"
#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "exception.hh"
#include "ipv4_datagram.hh"
#include "parser.hh"

#include <iostream>
#include <ranges>
#include <utility>
#include <vector>

using namespace std;

auto NetworkInterface::make_arp( const uint16_t opcode,
                                 const EthernetAddress& target_ethernet_address,
                                 const uint32_t target_ip_address ) const noexcept -> ARPMessage
{
  ARPMessage arp;
  arp.opcode = opcode;
  arp.sender_ethernet_address = ethernet_address_;
  arp.sender_ip_address = ip_address_.ipv4_numeric();
  arp.target_ethernet_address = target_ethernet_address;
  arp.target_ip_address = target_ip_address;
  return arp;
}

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  const AddressNumeric next_hop_numeric { next_hop.ipv4_numeric() };
  if ( ARP_cache_.contains( next_hop_numeric ) ) {
    const EthernetAddress& dst { ARP_cache_[next_hop_numeric].first };
    return transmit( { { dst, ethernet_address_, EthernetHeader::TYPE_IPv4 }, serialize( dgram ) } );
  }
  dgrams_waitting_[next_hop_numeric].emplace_back( dgram );
  if ( waitting_timer_.contains( next_hop_numeric ) ) {
    return;
  }
  waitting_timer_.emplace( next_hop_numeric, NetworkInterface::Timer {} );
  const ARPMessage arp_request { make_arp( ARPMessage::OPCODE_REQUEST, {}, next_hop_numeric ) };
  transmit( { { ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP }, serialize( arp_request ) } );
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  if ( frame.header.dst != ethernet_address_ and frame.header.dst != ETHERNET_BROADCAST ) {
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram ipv4_datagram;
    if ( parse( ipv4_datagram, frame.payload ) ) {
      datagrams_received_.emplace( move( ipv4_datagram ) );
    }
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage msg;
    if ( not parse( msg, frame.payload ) ) {
      return;
    }

    const AddressNumeric sender_ip { msg.sender_ip_address };
    const EthernetAddress sender_eth { msg.sender_ethernet_address };
    ARP_cache_[sender_ip] = { sender_eth, Timer {} };

    if ( msg.opcode == ARPMessage::OPCODE_REQUEST and msg.target_ip_address == ip_address_.ipv4_numeric() ) {
      const ARPMessage arp_reply { make_arp( ARPMessage::OPCODE_REPLY, sender_eth, sender_ip ) };
      transmit( { { sender_eth, ethernet_address_, EthernetHeader::TYPE_ARP }, serialize( arp_reply ) } );
    }
    if ( dgrams_waitting_.contains( sender_ip ) ) {
      for ( const auto& dgram : dgrams_waitting_[sender_ip] ) {
        transmit( { { sender_eth, ethernet_address_, EthernetHeader::TYPE_IPv4 }, serialize( dgram ) } );
      }
      dgrams_waitting_.erase( sender_ip );
      waitting_timer_.erase( sender_ip );
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  erase_if( ARP_cache_, [&]( auto&& item ) noexcept -> bool {
    return item.second.second.tick( ms_since_last_tick ).expired( ARP_ENTRY_TTL_ms );
  } );

  erase_if( waitting_timer_, [&]( auto&& item ) noexcept -> bool {
    return item.second.tick( ms_since_last_tick ).expired( ARP_RESPONSE_TTL_ms );
  } );
}