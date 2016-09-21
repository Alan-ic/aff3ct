#ifndef LAUNCHER_GEN_POLAR_HPP_
#define LAUNCHER_GEN_POLAR_HPP_

#include "../Launcher_GEN.hpp"

template <typename B, typename R, typename Q>
class Launcher_GEN_polar : public Launcher_GEN<B,R,Q>
{
public:
	Launcher_GEN_polar(const int argc, const char **argv, std::ostream &stream = std::cout);
	virtual ~Launcher_GEN_polar() {};

protected:
	virtual void build_args();
	virtual void store_args();
	virtual Simulation* build_simu();

	virtual std::vector<std::pair<std::string,std::string>> header_simulation();
	virtual std::vector<std::pair<std::string,std::string>> header_code      ();
	virtual std::vector<std::pair<std::string,std::string>> header_decoder   ();
};

#endif /* LAUNCHER_GEN_POLAR_HPP_ */
