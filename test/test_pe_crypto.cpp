/*

Copyright (c) 2007, Un Shyam
Copyright (c) 2007-2008, 2011, 2013-2019, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2018, Steven Siloti
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include <algorithm>
#include <iostream>

#include "libtorrent/hasher.hpp"
#include "libtorrent/hex.hpp"
#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/span.hpp"

#include "test.hpp"

#if !defined TORRENT_DISABLE_ENCRYPTION

namespace {

void test_enc_handler(lt::crypto_plugin& a, lt::crypto_plugin& b)
{
	int const repcount = 128;
	for (int rep = 0; rep < repcount; ++rep)
	{
		std::ptrdiff_t const buf_len = lt::random(512 * 1024);
		std::vector<char> buf(static_cast<std::size_t>(buf_len));
		std::vector<char> cmp_buf(static_cast<std::size_t>(buf_len));

		lt::aux::random_bytes(buf);
		std::copy(buf.begin(), buf.end(), cmp_buf.begin());

		using namespace lt::aux;

		{
			lt::span<char> iovec(buf.data(), buf_len);
			int next_barrier;
			lt::span<lt::span<char const>> iovec_out;
			std::tie(next_barrier, iovec_out) = a.encrypt(iovec);
			TEST_CHECK(buf != cmp_buf);
			TEST_EQUAL(iovec_out.size(), 0);
			TEST_EQUAL(next_barrier, int(buf_len));
		}

		{
			int consume = 0;
			int produce = 0;
			int packet_size = 0;
			lt::span<char> iovec(buf.data(), buf_len);
			std::tie(consume, produce, packet_size) = b.decrypt(iovec);
			TEST_CHECK(buf == cmp_buf);
			TEST_EQUAL(consume, 0);
			TEST_EQUAL(produce, int(buf_len));
			TEST_EQUAL(packet_size, 0);
		}

		{
			lt::span<char> iovec(buf.data(), buf_len);
			int next_barrier;
			lt::span<lt::span<char const>> iovec_out;
			std::tie(next_barrier, iovec_out) = b.encrypt(iovec);
			TEST_EQUAL(iovec_out.size(), 0);
			TEST_CHECK(buf != cmp_buf);
			TEST_EQUAL(next_barrier, int(buf_len));

			int consume = 0;
			int produce = 0;
			int packet_size = 0;
			lt::span<char> iovec2(buf.data(), buf_len);
			std::tie(consume, produce, packet_size) = a.decrypt(iovec2);
			TEST_CHECK(buf == cmp_buf);
			TEST_EQUAL(consume, 0);
			TEST_EQUAL(produce, int(buf_len));
			TEST_EQUAL(packet_size, 0);
		}
	}
}

} // anonymous namespace

TORRENT_TEST(diffie_hellman)
{
	using namespace lt;

	const int repcount = 128;

	for (int rep = 0; rep < repcount; ++rep)
	{
		dh_key_exchange DH1, DH2;

		TEST_CHECK(DH1.compute_secret(DH2.get_local_key()));
		TEST_CHECK(DH2.compute_secret(DH1.get_local_key()));

		TEST_EQUAL(DH1.get_secret(), DH2.get_secret());
		if (!DH1.get_secret() != DH2.get_secret())
		{
			std::printf("DH1 local: ");
			std::cout << DH1.get_local_key() << std::endl;

			std::printf("DH2 local: ");
			std::cout << DH2.get_local_key() << std::endl;

			std::printf("DH1 shared_secret: ");
			std::cout << DH1.get_secret() << std::endl;

			std::printf("DH2 shared_secret: ");
			std::cout << DH2.get_secret() << std::endl;
		}
	}
}

TORRENT_TEST(diffie_hellman_degenerate_key)
{
	// MODP DH prime (BEP 8) used by dh_key_exchange. The generator is 2.
	lt::key_t const dh_prime
		("0xFFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A63A36210000000000090563");

	// public keys outside [2, p-2] are degenerate and must be rejected.
	// Otherwise an attacker can fix the shared secret to a small set of
	// known values, defeating the encryption.
	{
		lt::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(lt::key_t(0)));
	}
	{
		lt::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(lt::key_t(1)));
	}
	{
		lt::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(dh_prime - 1));
	}
	{
		lt::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(dh_prime));
	}

	// boundary values inside the valid range must be accepted.
	{
		lt::dh_key_exchange dh;
		TEST_CHECK(dh.compute_secret(lt::key_t(2)));
	}
	{
		lt::dh_key_exchange dh;
		TEST_CHECK(dh.compute_secret(dh_prime - 2));
	}
}

TORRENT_TEST(rc4)
{
	using namespace lt;

	sha1_hash test1_key = hasher("test1_key",8).final();
	sha1_hash test2_key = hasher("test2_key",8).final();

	std::printf("testing RC4 handler\n");
	rc4_handler rc41;
	rc41.set_incoming_key(test2_key);
	rc41.set_outgoing_key(test1_key);
	rc4_handler rc42;
	rc42.set_incoming_key(test1_key);
	rc42.set_outgoing_key(test2_key);
	test_enc_handler(rc41, rc42);
}

// the keystream of the key 0x0102030405, from RFC 6229, at the offsets that
// follow the 1024 bytes that rc4_handler discards
TORRENT_TEST(rc4_keystream)
{
	using namespace lt;

	struct { int offset; char const* hex; } const vectors[] = {
		{1024, "30abbcc7c20b01609f23ee2d5f6bb7df"},
		{1520, "3294f744d8f9790507e70f62e5bbceea"},
		{1536, "d8729db41882259bee4f825325f5a130"},
		{2032, "1eb14a0c13b3bf47fa2a0ba93ad45b8b"},
		{2048, "cc582f8ba9f265e2b1be9112e975d2d7"},
		{3056, "f2e30f9bd102ecbf75aaade9bc35c43c"},
		{3072, "ec0e11c479dc329dc8da7968fe965681"},
		{4080, "068326a2118416d21f9d04b2cd1ca050"},
		{4096, "ff25b58995996707e51fbdf08b34d875"},
	};

	char const key[] = {1, 2, 3, 4, 5};
	rc4_handler rc4;
	rc4.set_incoming_key(key);
	rc4.set_outgoing_key(key);

	// XOR zeros with the keystream, in pieces of 1 to 23 bytes, so that the
	// pieces start and end at each offset within 8 bytes. encrypt() gets
	// each piece in a call of its own, decrypt() all of them in one call
	int const size = 4096 + 16 - 1024;
	std::vector<char> encrypted(std::size_t(size), 0);
	std::vector<char> decrypted(std::size_t(size), 0);
	std::vector<span<char>> pieces;
	int pos = 0;
	for (int len = 1; pos < size; len = len % 23 + 1)
	{
		int const n = std::min(len, size - pos);
		span<char> piece(encrypted.data() + pos, n);
		rc4.encrypt(piece);
		pieces.emplace_back(decrypted.data() + pos, n);
		pos += n;
	}
	rc4.decrypt(pieces);

	for (auto const& v : vectors)
	{
		TEST_EQUAL(aux::to_hex({encrypted.data() + v.offset - 1024, 16}), v.hex);
		TEST_EQUAL(aux::to_hex({decrypted.data() + v.offset - 1024, 16}), v.hex);
	}
}

// the whole send buffer is passed to encrypt(), which may be a lot more than
// what's left to the next barrier. Only the bytes ahead of the barrier may be
// encrypted
TORRENT_TEST(send_barrier_many_buffers)
{
	using namespace lt;

	sha1_hash const key = hasher("test1_key", 8).final();
	auto rc4 = std::make_shared<rc4_handler>();
	rc4->set_incoming_key(key);
	rc4->set_outgoing_key(key);

	// encrypt the next 68 bytes, then go back to plaintext. This is what an
	// outgoing connection does to send its handshake
	encryption_handler enc;
	TEST_CHECK(enc.switch_send_crypto(rc4, 0));
	TEST_CHECK(!enc.switch_send_crypto(nullptr, 68));
	TEST_CHECK(enc.is_send_plaintext());

	// more buffers than TORRENT_ALLOCA() keeps on the stack
	int const num_bufs = 1000;
	int const buf_size = 17;
	std::vector<char> data(std::size_t(num_bufs * buf_size), 'a');
	std::vector<span<char>> iovec;
	for (int i = 0; i < num_bufs; ++i)
		iovec.emplace_back(data.data() + i * buf_size, buf_size);

	int next_barrier;
	span<span<char const>> out_iovec;
	std::tie(next_barrier, out_iovec) = enc.encrypt(iovec);
	TEST_EQUAL(next_barrier, INT_MAX);
	TEST_CHECK(out_iovec.empty());

	TEST_CHECK(std::count(data.begin(), data.begin() + 68, 'a') < 68);
	TEST_EQUAL(std::count(data.begin() + 68, data.end(), 'a'), int(data.size()) - 68);

	span<char> encrypted(data.data(), 68);
	rc4->decrypt(encrypted);
	TEST_EQUAL(std::count(data.begin(), data.end(), 'a'), int(data.size()));
}

#else
TORRENT_TEST(disabled)
{
	std::printf("PE test not run because it's disabled\n");
}
#endif
