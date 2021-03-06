/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/****************************************************************************

   HttpServerSession.h

   Description:


 ****************************************************************************/

#pragma once

#include "P_Net.h"

#include "HttpConnectionCount.h"
#include "HttpProxyAPIEnums.h"

class HttpSM;
class MIOBuffer;
class IOBufferReader;

enum HSS_State {
  HSS_INIT,
  HSS_ACTIVE,
  HSS_KA_CLIENT_SLAVE,
  HSS_KA_SHARED,
};

enum {
  HTTP_SS_MAGIC_ALIVE = 0x0123FEED,
  HTTP_SS_MAGIC_DEAD  = 0xDEADFEED,
};

class HttpServerSession : public VConnection
{
  using self_type  = HttpServerSession;
  using super_type = VConnection;

public:
  HttpServerSession() : super_type(nullptr) {}
  HttpServerSession(self_type const &) = delete;
  self_type &operator=(self_type const &) = delete;

  void destroy();
  void new_connection(NetVConnection *new_vc);

  /** Enable tracking the number of outbound session.
   *
   * @param group The connection tracking group.
   *
   * The @a group must have already incremented the connection count. It will be cleaned up when the
   * session terminates.
   */
  void enable_outbound_connection_tracking(OutboundConnTrack::Group *group);

  IOBufferReader *
  get_reader()
  {
    return buf_reader;
  };

  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = nullptr) override;

  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = nullptr,
                   bool owner = false) override;

  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;

  void reenable(VIO *vio) override;

  void release();
  void attach_hostname(const char *hostname);
  NetVConnection *
  get_netvc() const
  {
    return server_vc;
  };
  void
  set_netvc(NetVConnection *new_vc)
  {
    server_vc = new_vc;
  }

  // Keys for matching hostnames
  IpEndpoint const &
  get_server_ip() const
  {
    ink_release_assert(server_vc != nullptr);
    return server_vc->get_remote_endpoint();
  }

  CryptoHash hostname_hash;

  int64_t con_id     = 0;
  int transact_count = 0;
  HSS_State state    = HSS_INIT;

  // Used to determine whether the session is for parent proxy
  // it is session to orgin server
  // We need to determine whether a closed connection was to
  // close parent proxy to update the
  // proxy.process.http.current_parent_proxy_connections
  bool to_parent_proxy = false;

  // Used to verify we are recording the server
  //   transaction stat properly
  int server_trans_stat = 0;

  // Sessions become if authentication headers
  //  are sent over them
  bool private_session = false;

  // Copy of the owning SM's server session sharing settings
  TSServerSessionSharingMatchType sharing_match = TS_SERVER_SESSION_SHARING_MATCH_BOTH;
  TSServerSessionSharingPoolType sharing_pool   = TS_SERVER_SESSION_SHARING_POOL_GLOBAL;
  //  int share_session;

  /// Hash map descriptor class for IP map.
  struct IPLinkage {
    self_type *_next = nullptr;
    self_type *_prev = nullptr;

    static self_type *&next_ptr(self_type *);
    static self_type *&prev_ptr(self_type *);
    static uint32_t hash_of(sockaddr const *key);
    static sockaddr const *key_of(self_type const *ssn);
    static bool equal(sockaddr const *lhs, sockaddr const *rhs);
    // Add a couple overloads for internal convenience.
    static bool equal(sockaddr const *lhs, HttpServerSession const *rhs);
    static bool equal(HttpServerSession const *lhs, sockaddr const *rhs);
  } _ip_link;

  /// Hash map descriptor class for FQDN map.
  struct FQDNLinkage {
    self_type *_next = nullptr;
    self_type *_prev = nullptr;

    static self_type *&next_ptr(self_type *);
    static self_type *&prev_ptr(self_type *);
    static uint64_t hash_of(CryptoHash const &key);
    static CryptoHash const &key_of(self_type *ssn);
    static bool equal(CryptoHash const &lhs, CryptoHash const &rhs);
  } _fqdn_link;

  // Keep track of connection limiting and a pointer to the
  // singleton that keeps track of the connection counts.
  OutboundConnTrack::Group *conn_track_group = nullptr;

  // The ServerSession owns the following buffer which use
  //   for parsing the headers.  The server session needs to
  //   own the buffer so we can go from a keep-alive state
  //   to being acquired and parsing the header without
  //   changing the buffer we are doing I/O on.  We can
  //   not change the buffer for I/O without issuing a
  //   an asyncronous cancel on NT
  MIOBuffer *read_buffer = nullptr;

  virtual int
  populate_protocol(std::string_view *result, int size) const
  {
    auto vc = this->get_netvc();
    return vc ? vc->populate_protocol(result, size) : 0;
  }

  virtual const char *
  protocol_contains(std::string_view tag_prefix) const
  {
    auto vc = this->get_netvc();
    return vc ? vc->protocol_contains(tag_prefix) : nullptr;
  }

private:
  NetVConnection *server_vc = nullptr;
  int magic                 = HTTP_SS_MAGIC_DEAD;

  IOBufferReader *buf_reader = nullptr;
};

extern ClassAllocator<HttpServerSession> httpServerSessionAllocator;

// --- Implementation ---

inline void
HttpServerSession::attach_hostname(const char *hostname)
{
  if (CRYPTO_HASH_ZERO == hostname_hash) {
    CryptoContext().hash_immediate(hostname_hash, (unsigned char *)hostname, strlen(hostname));
  }
}

inline HttpServerSession *&
HttpServerSession::IPLinkage::next_ptr(self_type *ssn)
{
  return ssn->_ip_link._next;
}

inline HttpServerSession *&
HttpServerSession::IPLinkage::prev_ptr(self_type *ssn)
{
  return ssn->_ip_link._prev;
}

inline uint32_t
HttpServerSession::IPLinkage::hash_of(sockaddr const *key)
{
  return ats_ip_hash(key);
}

inline sockaddr const *
HttpServerSession::IPLinkage::key_of(self_type const *ssn)
{
  return &ssn->get_server_ip().sa;
}

inline bool
HttpServerSession::IPLinkage::equal(sockaddr const *lhs, sockaddr const *rhs)
{
  return ats_ip_addr_port_eq(lhs, rhs);
}

inline bool
HttpServerSession::IPLinkage::equal(sockaddr const *lhs, HttpServerSession const *rhs)
{
  return ats_ip_addr_port_eq(lhs, key_of(rhs));
}

inline bool
HttpServerSession::IPLinkage::equal(HttpServerSession const *lhs, sockaddr const *rhs)
{
  return ats_ip_addr_port_eq(key_of(lhs), rhs);
}

inline HttpServerSession *&
HttpServerSession::FQDNLinkage::next_ptr(self_type *ssn)
{
  return ssn->_fqdn_link._next;
}

inline HttpServerSession *&
HttpServerSession::FQDNLinkage::prev_ptr(self_type *ssn)
{
  return ssn->_fqdn_link._prev;
}

inline uint64_t
HttpServerSession::FQDNLinkage::hash_of(CryptoHash const &key)
{
  return key.fold();
}

inline CryptoHash const &
HttpServerSession::FQDNLinkage::key_of(self_type *ssn)
{
  return ssn->hostname_hash;
}

inline bool
HttpServerSession::FQDNLinkage::equal(CryptoHash const &lhs, CryptoHash const &rhs)
{
  return lhs == rhs;
}
