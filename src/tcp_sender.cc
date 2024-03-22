#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return sequence_numbers_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return my_timer.peek_count();
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // 1.达到最大传输字节数（窗口大小）
  // 2.仅传输中的序列号没了且window_size=0才需要发送假消息，如果还有序列号才传输中，可以利用这些得到ack更新size（传输失败就重传）
  if ( ( report_window_size && sequence_numbers_in_flight_ >= report_window_size )
       || ( report_window_size == 0 && sequence_numbers_in_flight_ >= 1 ) ) {
    return;
  }

  auto seqno = Wrap32::wrap( abs_sender_num, isn_ );

  // 限制从buffer中取出来的字节数
  auto win = report_window_size == 0
               ? 1
               : report_window_size - sequence_numbers_in_flight_ - static_cast<uint16_t>( seqno == isn_ );

  string out;
  while ( reader().bytes_buffered() and static_cast<uint16_t>( out.size() ) < win ) {
    auto view = reader().peek();

    if ( view.empty() ) {
      throw std::runtime_error( "Reader::peek() returned empty string_view" );
    }

    view = view.substr( 0, win - out.size() ); // Don't return more bytes than desired.
    out += view;
    input_.reader().pop( view.size() );
  }

  size_t len;
  string_view view( out );

  while ( !view.empty() || seqno == isn_ || ( !FIN_ && writer().is_closed() ) ) {
    len = min( view.size(), TCPConfig::MAX_PAYLOAD_SIZE );

    string payload( view.substr( 0, len ) );

    TCPSenderMessage message { seqno, seqno == isn_, move( payload ), false, writer().has_error() };

    // 1.当前窗口大小限制携带不了FIN，留着以后发，没有新的消息了直接退出，否则携带
    // 2.zero窗口仅当message为0时才能携带（因为视为窗口大小为1）
    if ( !FIN_ && writer().is_closed() && len == view.size()
         && ( sequence_numbers_in_flight_ + message.sequence_length() < report_window_size
              || ( report_window_size == 0 && message.sequence_length() == 0 ) ) ) {
      FIN_ = message.FIN = true;
    }

    transmit( message );
    if ( !my_timer.is_running() && message.sequence_length() ) {
      auto RTO = my_timer.get_current_RTO() ? my_timer.get_current_RTO() : initial_RTO_ms_;
      my_timer.state_reset( RTO );
    }
    abs_sender_num += message.sequence_length();
    sequence_numbers_in_flight_ += message.sequence_length();
    my_sender_queue.emplace( move( message ) );

    // 当前窗口大小限制携带不了FIN，留着以后发，没有新的消息了直接退出
    if ( !FIN_ && writer().is_closed() && len == view.size() ) {
      break;
    }

    seqno = Wrap32::wrap( abs_sender_num, isn_ );
    view.remove_prefix( len );
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return { Wrap32::wrap( abs_sender_num, isn_ ), false, "", false, writer().has_error() };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) {
    writer().set_error();
    return;
  }

  // treat a '0' window size as equal to '1' but don't back off RTO
  report_window_size = msg.window_size;
  uint64_t abs_seq_k = msg.ackno ? msg.ackno.value().unwrap( isn_, abs_acked_num ) : 0;

  if ( abs_seq_k > abs_acked_num && abs_seq_k <= abs_sender_num ) {
    abs_acked_num = abs_seq_k;

    my_timer.state_reset( initial_RTO_ms_ );
    my_timer.clear_count();

    uint64_t abs_seq = 0;
    while ( !my_sender_queue.empty() && abs_seq <= abs_seq_k ) {
      abs_seq
        = my_sender_queue.front().seqno.unwrap( isn_, abs_acked_num ) + my_sender_queue.front().sequence_length();
      if ( abs_seq <= abs_seq_k ) {
        sequence_numbers_in_flight_ -= my_sender_queue.front().sequence_length();
        my_sender_queue.pop();
      }
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // timer stopped when queue is empty
  if ( !my_sender_queue.empty() ) {
    // 如果在重传之前收到ack并且队列为空，则不需要重传了，此时timer相关信息已经清0
    if ( my_timer.check_out_of_date( ms_since_last_tick ) ) {
      transmit( my_sender_queue.front() );
      if ( report_window_size > 0 ) {
        my_timer.add_count();
        my_timer.state_reset( 2 * my_timer.get_current_RTO() );
        return;
      }
      my_timer.state_reset( my_timer.get_current_RTO() );
    }
  }
}