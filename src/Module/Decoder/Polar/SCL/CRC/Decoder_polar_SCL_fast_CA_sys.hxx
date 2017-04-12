#include "Decoder_polar_SCL_fast_CA_sys.hpp"

namespace aff3ct
{
namespace module
{
template <typename B, typename R, class API_polar>
Decoder_polar_SCL_fast_CA_sys<B,R,API_polar>
::Decoder_polar_SCL_fast_CA_sys(const int& K, const int& N, const int& L, const mipp::vector<B>& frozen_bits, CRC<B>& crc,
                                const int n_frames, const std::string name)
: Decoder_polar_SCL_fast_sys<B,R,API_polar>(K, N, L, frozen_bits, n_frames, name),
  fast_store(false), crc(crc), U_test(K)
{
}

template <typename B, typename R, class API_polar>
Decoder_polar_SCL_fast_CA_sys<B,R,API_polar>
::Decoder_polar_SCL_fast_CA_sys(const int& K, const int& N, const int& L, const mipp::vector<B>& frozen_bits,
                                const std::vector<tools::Pattern_polar_i*> polar_patterns,
                                const int idx_r0, const int idx_r1,
                                CRC<B>& crc, const int n_frames, const std::string name)
: Decoder_polar_SCL_fast_sys<B,R,API_polar>(K, N, L, frozen_bits, polar_patterns, idx_r0, idx_r1, n_frames, name),
  fast_store(false), crc(crc), U_test(K)
{
}

template <typename B, typename R, class API_polar>
bool Decoder_polar_SCL_fast_CA_sys<B,R,API_polar>
::crc_check(mipp::vector<B> &s)
{
	this->polar_patterns.fb_extract(s.data(), U_test.data());

	// check the CRC
	return crc.check(U_test, this->get_simd_inter_frame_level());
}

template <typename B, typename R, class API_polar>
int Decoder_polar_SCL_fast_CA_sys<B,R,API_polar>
::select_best_path()
{
	std::sort(this->paths.begin(), this->paths.begin() + this->n_active_paths,
		[this](int x, int y){
			return this->metrics[x] < this->metrics[y];
		});

	auto i = 0;
	while (i < this->n_active_paths && !crc_check(this->s[this->paths[i]])) i++;

	this->best_path = (i == this->n_active_paths) ? this->paths[0] : this->paths[i];
	fast_store = i != this->n_active_paths;

	return this->n_active_paths -i;
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_CA_sys<B,R,API_polar>
::init_buffers()
{
	Decoder_polar_SCL_fast_sys<B,R,API_polar>::init_buffers();
	fast_store = false;
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_CA_sys<B,R,API_polar>
::_store(B *V_K) const
{
	if (fast_store)
		std::copy(U_test.begin(), U_test.begin() + this->K, V_K);
	else
		Decoder_polar_SCL_fast_sys<B,R,API_polar>::_store(V_K);
}
}
}