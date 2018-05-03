#include "Tools/Exception/exception.hpp"
#include "Tools/Math/max.h"
#include "Tools/Code/LDPC/Matrix_handler/LDPC_matrix_handler.hpp"

#include "Module/Decoder/LDPC/BP/Flooding/SPA/Decoder_LDPC_BP_flooding_sum_product.hpp"
#include "Module/Decoder/LDPC/BP/Flooding/LSPA/Decoder_LDPC_BP_flooding_log_sum_product.hpp"
#include "Module/Decoder/LDPC/BP/Flooding/ONMS/Decoder_LDPC_BP_flooding_offset_normalize_min_sum.hpp"
#include "Module/Decoder/LDPC/BP/Flooding/AMS/Decoder_LDPC_BP_flooding_approximate_min_star.hpp"
#include "Module/Decoder/LDPC/BP/Flooding/Gallager/Decoder_LDPC_BP_flooding_Gallager_A.hpp"
#include "Module/Decoder/LDPC/BP/Layered/SPA/Decoder_LDPC_BP_layered_sum_product.hpp"
#include "Module/Decoder/LDPC/BP/Layered/LSPA/Decoder_LDPC_BP_layered_log_sum_product.hpp"
#include "Module/Decoder/LDPC/BP/Layered/ONMS/Decoder_LDPC_BP_layered_offset_normalize_min_sum.hpp"
#include "Module/Decoder/LDPC/BP/Layered/ONMS/Decoder_LDPC_BP_layered_ONMS_inter.hpp"
#include "Module/Decoder/LDPC/BP/Layered/AMS/Decoder_LDPC_BP_layered_approximate_min_star.hpp"
#include "Module/Decoder/LDPC/BP/Peeling/Decoder_LDPC_BP_peeling.hpp"

#include "Decoder_LDPC.hpp"

using namespace aff3ct;
using namespace aff3ct::factory;

const std::string aff3ct::factory::Decoder_LDPC_name   = "Decoder LDPC";
const std::string aff3ct::factory::Decoder_LDPC_prefix = "dec";

Decoder_LDPC::parameters
::parameters(const std::string &prefix)
: Decoder::parameters(Decoder_LDPC_name, prefix)
{
	this->type   = "BP_FLOODING";
	this->implem = "SPA";
}

Decoder_LDPC::parameters
::~parameters()
{
}

Decoder_LDPC::parameters* Decoder_LDPC::parameters
::clone() const
{
	return new Decoder_LDPC::parameters(*this);
}

void Decoder_LDPC::parameters
::get_description(tools::Argument_map_info &args) const
{
	Decoder::parameters::get_description(args);

	auto p = this->get_prefix();

	args.add(
		{p+"-h-path"},
		tools::File(tools::openmode::read),
		"path to the H matrix (AList or QC formated file).",
		tools::arg_rank::REQ);

	args.add_link({p+"-h-path"}, {p+"-cw-size",   "N"}); // N_cw is H width
	args.add_link({p+"-h-path"}, {p+"-info-bits", "K"}); // if there is no K, then H is considered regular,
	                                                     // so K is the N - H's height


	tools::add_options(args.at({p+"-type", "D"}), 0, "BP", "BP_FLOODING", "BP_LAYERED", "BP_PEELING");
	tools::add_options(args.at({p+"-implem"   }), 0, "ONMS", "SPA", "LSPA", "GALA", "AMS");

	args.add(
		{p+"-ite", "i"},
		tools::Integer(tools::Positive()),
		"maximal number of iterations in the LDPC decoder.");

	args.add(
		{p+"-off"},
		tools::Real(),
		"offset used in the offset min-sum BP algorithm (works only with \"--dec-implem ONMS\").");

	args.add(
		{p+"-norm"},
		tools::Real(tools::Positive()),
		"normalization factor used in the normalized min-sum BP algorithm (works only with \"--dec-implem ONMS\").");

	args.add(
		{p+"-no-synd"},
		tools::None(),
		"disable the syndrome detection (disable the stop criterion in the LDPC decoders).");

	args.add(
		{p+"-synd-depth"},
		tools::Integer(tools::Positive(), tools::Non_zero()),
		"successive number of iterations to validate the syndrome detection.");

	args.add(
		{p+"-simd"},
		tools::Text(tools::Including_set("INTER")),
		"the SIMD strategy you want to use.");

	args.add(
		{p+"-min"},
		tools::Text(tools::Including_set("MIN", "MINL", "MINS")),
		"the MIN implementation for the nodes (AMS decoder).");

	args.add(
		{p+"-h-reorder"},
		tools::Text(tools::Including_set("NONE", "ASC", "DSC")),
		"specify if the check nodes (CNs) from H have to be reordered, 'NONE': do nothing (default), 'ASC': from the "
		"smallest to the biggest CNs, 'DSC': from the biggest to the smallest CNs.");
}

void Decoder_LDPC::parameters
::store(const tools::Argument_map_value &vals)
{
	auto p = this->get_prefix();

	if(vals.exist({p+"-h-path"    })) this->H_path          = vals.at      ({p+"-h-path"    });
	if(vals.exist({p+"-h-reorder" })) this->H_reorder       = vals.at      ({p+"-h-reorder" });
	if(vals.exist({p+"-simd"      })) this->simd_strategy   = vals.at      ({p+"-simd"      });
	if(vals.exist({p+"-min"       })) this->min             = vals.at      ({p+"-min"       });
	if(vals.exist({p+"-ite",   "i"})) this->n_ite           = vals.to_int  ({p+"-ite",   "i"});
	if(vals.exist({p+"-synd-depth"})) this->syndrome_depth  = vals.to_int  ({p+"-synd-depth"});
	if(vals.exist({p+"-off"       })) this->offset          = vals.to_float({p+"-off"       });
	if(vals.exist({p+"-norm"      })) this->norm_factor     = vals.to_float({p+"-norm"      });
	if(vals.exist({p+"-no-synd"   })) this->enable_syndrome = false;

	if (!this->H_path.empty())
	{
		int H;
		tools::LDPC_matrix_handler::read_matrix_size(this->H_path, H, this->N_cw);
		this->K = this->N_cw - H; // considered as regular so H = N - K
	}

	Decoder::parameters::store(vals);
}

void Decoder_LDPC::parameters
::get_headers(std::map<std::string,header_list>& headers, const bool full) const
{
	Decoder::parameters::get_headers(headers, full);

	if (this->type != "ML" && this->type != "CHASE")
	{
		auto p = this->get_prefix();

		if (!this->H_path.empty())
		{
			headers[p].push_back(std::make_pair("H matrix path", this->H_path));
			headers[p].push_back(std::make_pair("H matrix reordering", this->H_reorder));
		}

		if (!this->simd_strategy.empty())
			headers[p].push_back(std::make_pair("SIMD strategy", this->simd_strategy));

		headers[p].push_back(std::make_pair("Num. of iterations (i)", std::to_string(this->n_ite)));

		if (this->implem == "ONMS")
		{
			headers[p].push_back(std::make_pair("Offset", std::to_string(this->offset)));
			headers[p].push_back(std::make_pair("Normalize factor", std::to_string(this->norm_factor)));
		}

		std::string syndrome = this->enable_syndrome ? "on" : "off";
		headers[p].push_back(std::make_pair("Stop criterion (syndrome)", syndrome));

		if (this->enable_syndrome)
			headers[p].push_back(std::make_pair("Stop criterion depth", std::to_string(this->syndrome_depth)));

		if (this->implem == "AMS")
			headers[p].push_back(std::make_pair("Min type", this->min));
	}
}

template <typename B, typename Q>
module::Decoder_SISO_SIHO<B,Q>* Decoder_LDPC::parameters
::build_siso(const tools::Sparse_matrix &H, const std::vector<unsigned> &info_bits_pos,
             module::Encoder<B> *encoder) const
{
	if ((this->type == "BP" || this->type == "BP_FLOODING") && this->simd_strategy.empty())
	{
		     if (this->implem == "ONMS") return new module::Decoder_LDPC_BP_flooding_ONMS     <B,Q>(this->K, this->N_cw, this->n_ite, H, info_bits_pos, this->norm_factor, (Q)this->offset, this->enable_syndrome, this->syndrome_depth, this->n_frames);
		else if (this->implem == "SPA" ) return new module::Decoder_LDPC_BP_flooding_SPA      <B,Q>(this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
		else if (this->implem == "LSPA") return new module::Decoder_LDPC_BP_flooding_LSPA     <B,Q>(this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
		else if (this->implem == "AMS" ) {
			if (this->min == "MIN")
				return new module::Decoder_LDPC_BP_flooding_AMS<B,Q,tools::min<Q>>                 (this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
			else if (this->min == "MINL")
				return new module::Decoder_LDPC_BP_flooding_AMS<B,Q,tools::min_star_linear2<Q>>    (this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
			else if (this->min == "MINS")
				return new module::Decoder_LDPC_BP_flooding_AMS<B,Q,tools::min_star<Q>>            (this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
		}
	}
	else if (this->type == "BP_LAYERED" && this->simd_strategy.empty())
	{
		     if (this->implem == "ONMS") return new module::Decoder_LDPC_BP_layered_ONMS      <B,Q>(this->K, this->N_cw, this->n_ite, H, info_bits_pos, this->norm_factor, (Q)this->offset, this->enable_syndrome, this->syndrome_depth, this->n_frames);
		else if (this->implem == "SPA" ) return new module::Decoder_LDPC_BP_layered_SPA       <B,Q>(this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
		else if (this->implem == "LSPA") return new module::Decoder_LDPC_BP_layered_LSPA      <B,Q>(this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
		else if (this->implem == "AMS" ) {
			if (this->min == "MIN")
				return new module::Decoder_LDPC_BP_layered_AMS<B,Q,tools::min<Q>>                  (this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
			else if (this->min == "MINL")
				return new module::Decoder_LDPC_BP_layered_AMS<B,Q,tools::min_star_linear2<Q>>     (this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
			else if (this->min == "MINS")
				return new module::Decoder_LDPC_BP_layered_AMS<B,Q,tools::min_star<Q>>             (this->K, this->N_cw, this->n_ite, H, info_bits_pos,                                     this->enable_syndrome, this->syndrome_depth, this->n_frames);
		}
	}
	else if (this->type == "BP_LAYERED" && this->simd_strategy == "INTER")
	{
		     if (this->implem == "ONMS") return new module::Decoder_LDPC_BP_layered_ONMS_inter<B,Q>(this->K, this->N_cw, this->n_ite, H, info_bits_pos, this->norm_factor, (Q)this->offset, this->enable_syndrome, this->syndrome_depth, this->n_frames);
	}
	else if (this->type == "BP_PEELING")
	{
		     if (this->implem == "STD") return new module::Decoder_LDPC_BP_peeling<B,Q>(this->K, this->N_cw, this->n_ite, H, info_bits_pos, this->enable_syndrome, this->syndrome_depth, false, this->n_frames);
	}

	throw tools::cannot_allocate(__FILE__, __LINE__, __func__);
}

template <typename B, typename Q>
module::Decoder_SIHO<B,Q>* Decoder_LDPC::parameters
::build(const tools::Sparse_matrix &H, const std::vector<unsigned> &info_bits_pos,
        module::Encoder<B> *encoder) const
{
	try
	{
		return Decoder::parameters::build<B,Q>(encoder);
	}
	catch (tools::cannot_allocate const&)
	{
		if ((this->type == "BP" || this->type == "BP_FLOODING") && this->simd_strategy.empty())
		{
			if (this->implem == "GALA") return new module::Decoder_LDPC_BP_flooding_GALA<B,Q>(this->K, this->N_cw, this->n_ite, H, info_bits_pos, this->enable_syndrome, this->syndrome_depth, this->n_frames);
		}

		return build_siso<B,Q>(H, info_bits_pos);
	}
}

template <typename B, typename Q>
module::Decoder_SISO_SIHO<B,Q>* Decoder_LDPC
::build_siso(const parameters& params, const tools::Sparse_matrix &H, const std::vector<unsigned> &info_bits_pos,
             module::Encoder<B> *encoder)
{
	return params.template build_siso<B,Q>(H, info_bits_pos, encoder);
}

template <typename B, typename Q>
module::Decoder_SIHO<B,Q>* Decoder_LDPC
::build(const parameters& params, const tools::Sparse_matrix &H, const std::vector<unsigned> &info_bits_pos,
        module::Encoder<B> *encoder)
{
	return params.template build<B,Q>(H, info_bits_pos, encoder);
}

// ==================================================================================== explicit template instantiation
#include "Tools/types.h"
#ifdef MULTI_PREC
template aff3ct::module::Decoder_SISO_SIHO<B_8 ,Q_8 >* aff3ct::factory::Decoder_LDPC::parameters::build_siso<B_8 ,Q_8 >(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_8 >*) const;
template aff3ct::module::Decoder_SISO_SIHO<B_16,Q_16>* aff3ct::factory::Decoder_LDPC::parameters::build_siso<B_16,Q_16>(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_16>*) const;
template aff3ct::module::Decoder_SISO_SIHO<B_32,Q_32>* aff3ct::factory::Decoder_LDPC::parameters::build_siso<B_32,Q_32>(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_32>*) const;
template aff3ct::module::Decoder_SISO_SIHO<B_64,Q_64>* aff3ct::factory::Decoder_LDPC::parameters::build_siso<B_64,Q_64>(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_64>*) const;
template aff3ct::module::Decoder_SISO_SIHO<B_8 ,Q_8 >* aff3ct::factory::Decoder_LDPC::build_siso<B_8 ,Q_8 >(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_8 >*);
template aff3ct::module::Decoder_SISO_SIHO<B_16,Q_16>* aff3ct::factory::Decoder_LDPC::build_siso<B_16,Q_16>(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_16>*);
template aff3ct::module::Decoder_SISO_SIHO<B_32,Q_32>* aff3ct::factory::Decoder_LDPC::build_siso<B_32,Q_32>(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_32>*);
template aff3ct::module::Decoder_SISO_SIHO<B_64,Q_64>* aff3ct::factory::Decoder_LDPC::build_siso<B_64,Q_64>(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_64>*);
#else
template aff3ct::module::Decoder_SISO_SIHO<B,Q>* aff3ct::factory::Decoder_LDPC::parameters::build_siso<B,Q>(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B>*) const;
template aff3ct::module::Decoder_SISO_SIHO<B,Q>* aff3ct::factory::Decoder_LDPC::build_siso<B,Q>(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B>*);
#endif

#include "Tools/types.h"
#ifdef MULTI_PREC
template aff3ct::module::Decoder_SIHO<B_8 ,Q_8 >* aff3ct::factory::Decoder_LDPC::parameters::build<B_8 ,Q_8 >(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_8 >*) const;
template aff3ct::module::Decoder_SIHO<B_16,Q_16>* aff3ct::factory::Decoder_LDPC::parameters::build<B_16,Q_16>(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_16>*) const;
template aff3ct::module::Decoder_SIHO<B_32,Q_32>* aff3ct::factory::Decoder_LDPC::parameters::build<B_32,Q_32>(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_32>*) const;
template aff3ct::module::Decoder_SIHO<B_64,Q_64>* aff3ct::factory::Decoder_LDPC::parameters::build<B_64,Q_64>(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_64>*) const;
template aff3ct::module::Decoder_SIHO<B_8 ,Q_8 >* aff3ct::factory::Decoder_LDPC::build<B_8 ,Q_8 >(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_8 >*);
template aff3ct::module::Decoder_SIHO<B_16,Q_16>* aff3ct::factory::Decoder_LDPC::build<B_16,Q_16>(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_16>*);
template aff3ct::module::Decoder_SIHO<B_32,Q_32>* aff3ct::factory::Decoder_LDPC::build<B_32,Q_32>(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_32>*);
template aff3ct::module::Decoder_SIHO<B_64,Q_64>* aff3ct::factory::Decoder_LDPC::build<B_64,Q_64>(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B_64>*);
#else
template aff3ct::module::Decoder_SIHO<B,Q>* aff3ct::factory::Decoder_LDPC::parameters::build<B,Q>(const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B>*) const;
template aff3ct::module::Decoder_SIHO<B,Q>* aff3ct::factory::Decoder_LDPC::build<B,Q>(const aff3ct::factory::Decoder_LDPC::parameters&, const aff3ct::tools::Sparse_matrix&, const std::vector<unsigned>&, module::Encoder<B>*);
#endif
// ==================================================================================== explicit template instantiation
