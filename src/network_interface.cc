#include <iostream>

#include "arp_message.hh"
#include "exception.hh"
#include "network_interface.hh"

using namespace std;

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
  auto ip = next_hop.ipv4_numeric();
  auto it = arp_hash_map.find( ip );
  if ( it == arp_hash_map.end() ) {
    if ( arp_cool_down ) {
      unarp_queue.push( { ip, dgram } );
      return;
    } else {
      EthernetHeader arp_header = { ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP };
      ARPMessage arp_message;
      arp_message.sender_ethernet_address = ethernet_address_;
      arp_message.sender_ip_address = ip_address_.ipv4_numeric();
      arp_message.target_ip_address = next_hop.ipv4_numeric();
      arp_message.opcode = ARPMessage::OPCODE_REQUEST;
      transmit( { arp_header, serialize( arp_message ) } );
      arp_cool_down = 5000;
      unarp_queue.push( { ip, dgram } );
    }

  } else {
    EthernetHeader send_header = { it->second.first, ethernet_address_, EthernetHeader::TYPE_IPv4 };
    transmit( { send_header, serialize( dgram ) } );
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  EthernetHeader frame_header = frame.header;
  EthernetHeader sender_header = { frame_header.src, ethernet_address_, 0 };

  if ( frame_header.type == EthernetHeader::TYPE_ARP
       && ( ( frame_header.dst == ethernet_address_ ) || ( frame_header.dst == ETHERNET_BROADCAST ) ) ) {
    ARPMessage arp_msg;
    if ( parse( arp_msg, frame.payload ) ) {
      arp_hash_map[arp_msg.sender_ip_address] = { arp_msg.sender_ethernet_address, 30000 };
      if ( arp_msg.opcode == ARPMessage::OPCODE_REQUEST
           && arp_msg.target_ip_address == ip_address_.ipv4_numeric() ) {
        ARPMessage sender_arp_msg;
        sender_arp_msg.opcode = ARPMessage::OPCODE_REPLY;
        sender_arp_msg.sender_ethernet_address = ethernet_address_;
        sender_arp_msg.sender_ip_address = ip_address_.ipv4_numeric();
        sender_arp_msg.target_ethernet_address = arp_msg.sender_ethernet_address;
        sender_arp_msg.target_ip_address = arp_msg.sender_ip_address;
        sender_header.type = EthernetHeader::TYPE_ARP;
        transmit( { sender_header, serialize( sender_arp_msg ) } );
      }

      while ( !unarp_queue.empty() && arp_hash_map.find( unarp_queue.front().ipv4 ) != arp_hash_map.end() ) {
        EthernetHeader resend_header
          = { arp_hash_map[unarp_queue.front().ipv4].first, ethernet_address_, EthernetHeader::TYPE_IPv4 };
        transmit( { resend_header, serialize( unarp_queue.front().dgram ) } );
        unarp_queue.pop();
      }
    }

  } else if ( frame_header.type == EthernetHeader::TYPE_IPv4 && frame_header.dst == ethernet_address_ ) {
    InternetDatagram data_msg;
    if ( parse( data_msg, frame.payload ) ) {
      datagrams_received_.push( data_msg );
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  arp_cool_down = arp_cool_down >= ms_since_last_tick ? arp_cool_down - ms_since_last_tick : 0;
  vector<uint32_t> keys_to_erase;
  for ( auto& [ipv4_, val] : arp_hash_map ) {
    auto& [eth_, time_] = val;
    if ( time_ <= ms_since_last_tick ) {
      keys_to_erase.push_back( ipv4_ );
    } else {
      time_ -= ms_since_last_tick;
    }
  }
  for ( auto& key : keys_to_erase ) {
    arp_hash_map.erase( key );
  }
}
