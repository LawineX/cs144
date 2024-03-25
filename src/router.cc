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

  match_vec.emplace_back(route_prefix,prefix_length,next_hop,interface_num);
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  for(auto & iter_interface: _interfaces){
    auto send_queue =iter_interface->datagrams_received();
    while(!send_queue.empty()){
      auto &dgram =send_queue.front();
      auto dst_ip = dgram.header.dst;
      match_struct *select_struct=nullptr;
      int max_len =-1;
      for(auto &rule_ :match_vec){
        if((dst_ip & rule_.route_prefix) ==rule_.route_prefix && rule_.prefix_length>max_len){
          select_struct = &rule_;
        }
      }
      if(dgram.header.ttl==1 || dgram.header.ttl==0 || select_struct==nullptr){
        send_queue.pop();
        continue;
      }
      --dgram.header.ttl;
      dgram.header.compute_checksum();
      auto send_inter = interface(static_cast<size_t>(select_struct->interface_num));
      if(select_struct->next_hop.has_value()){
        send_inter->send_datagram(dgram,select_struct->next_hop.value());
      }else{
        send_inter->send_datagram(dgram,Address::from_ipv4_numeric(dst_ip));
      }
      send_queue.pop();
    }
  }
}
