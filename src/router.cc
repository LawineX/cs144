#include "router.hh"

#include <iostream>
#include <limits>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  match_vec.push_back( {route_prefix, prefix_length, next_hop, interface_num} );
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  for ( auto& iter_interface : _interfaces ) {
    auto &&send_queue = move(iter_interface->datagrams_received());
    while ( !send_queue.empty() ) {
      auto dgram = move(send_queue.front());
      send_queue.pop();

      auto dst_ip = dgram.header.dst;
      int pos = -1;
      int max_len = -1;
      for ( size_t i = 0; i < match_vec.size(); ++i) {
        if ( ( match_vec[i].prefix_length==0 || (( dst_ip >> (32- match_vec[i].prefix_length) ) == (match_vec[i].route_prefix >> (32- match_vec[i].prefix_length))))  && match_vec[i].prefix_length > max_len ) {
          pos = i;
          max_len =match_vec[i].prefix_length;
        }
      }
      if ( dgram.header.ttl <=1 || max_len==-1 ) {
        continue;
      }
      --dgram.header.ttl;
      dgram.header.compute_checksum();
      auto send_inter = interface( static_cast<size_t>( match_vec[pos].interface_num ) );
      // send_inter->send_datagram( dgram, Address::from_ipv4_numeric( dst_ip ) );
      send_inter->send_datagram( dgram, match_vec[pos].next_hop.value_or(Address::from_ipv4_numeric( dst_ip )) );
    }
  }
}
