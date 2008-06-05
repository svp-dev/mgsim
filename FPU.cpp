#include "FPU.h"
#include "RegisterFile.h"
#include "Processor.h"
#include <cmath>
using namespace std;

namespace Simulator
{

bool FPU::idle() const
{
	return m_pipelines.empty();
}

bool FPU::queueOperation(int opcode, int func, const Float& Rav, const Float& Rbv, const RegAddr& Rc)
{
	CycleNo	latency;
	Result  res;

	if (opcode == A_OP_ITFP)
	{
		switch (func)
		{

			// VAX Floating Square Root
			case A_ITFPFUNC_SQRTF:
			case A_ITFPFUNC_SQRTF_C:
			case A_ITFPFUNC_SQRTF_S:
			case A_ITFPFUNC_SQRTF_SC:
			case A_ITFPFUNC_SQRTF_SU:
			case A_ITFPFUNC_SQRTF_SUC:
			case A_ITFPFUNC_SQRTF_U:
			case A_ITFPFUNC_SQRTF_UC:
				latency = m_config.sqrtLatency;
				break;

			case A_ITFPFUNC_SQRTG:
			case A_ITFPFUNC_SQRTG_C:
			case A_ITFPFUNC_SQRTG_S:
			case A_ITFPFUNC_SQRTG_SC:
			case A_ITFPFUNC_SQRTG_SU:
			case A_ITFPFUNC_SQRTG_SUC:
			case A_ITFPFUNC_SQRTG_U:
			case A_ITFPFUNC_SQRTG_UC:
				latency = m_config.sqrtLatency;
				break;

			// IEEE Floating Square Root
			case A_ITFPFUNC_SQRTS:
			case A_ITFPFUNC_SQRTS_C:
			case A_ITFPFUNC_SQRTS_D:
			case A_ITFPFUNC_SQRTS_M:
			case A_ITFPFUNC_SQRTS_SU:
			case A_ITFPFUNC_SQRTS_SUC:
			case A_ITFPFUNC_SQRTS_SUD:
			case A_ITFPFUNC_SQRTS_SUIC:
			case A_ITFPFUNC_SQRTS_SUID:
			case A_ITFPFUNC_SQRTS_SUIM:
			case A_ITFPFUNC_SQRTS_SUM:
			case A_ITFPFUNC_SQRTS_SUU:
			case A_ITFPFUNC_SQRTS_U:
			case A_ITFPFUNC_SQRTS_UC:
			case A_ITFPFUNC_SQRTS_UD:
			case A_ITFPFUNC_SQRTS_UM:
				res.value.fromfloat( sqrtf( Rbv.tofloat() ) );
				latency = m_config.sqrtLatency;
				//DebugSimWrite("sqrt(%f) = %f\n", Rbv.tofloat(), res.value.tofloat());
				break;

			case A_ITFPFUNC_SQRTT:
			case A_ITFPFUNC_SQRTT_C:
			case A_ITFPFUNC_SQRTT_D:
			case A_ITFPFUNC_SQRTT_M:
			case A_ITFPFUNC_SQRTT_SU:
			case A_ITFPFUNC_SQRTT_SUC:
			case A_ITFPFUNC_SQRTT_SUD:
			case A_ITFPFUNC_SQRTT_SUI:
			case A_ITFPFUNC_SQRTT_SUIC:
			case A_ITFPFUNC_SQRTT_SUID:
			case A_ITFPFUNC_SQRTT_SUIM:
			case A_ITFPFUNC_SQRTT_SUM:
			case A_ITFPFUNC_SQRTT_U:
			case A_ITFPFUNC_SQRTT_UC:
			case A_ITFPFUNC_SQRTT_UD:
			case A_ITFPFUNC_SQRTT_UM:
				res.value.fromdouble( sqrt( Rbv.todouble() ) );
				latency = m_config.sqrtLatency;
				//DebugSimWrite("sqrt(%lf) = %lf\n", Rbv.todouble(), res.value.todouble());
				break;
		}
	}
	else if (opcode == A_OP_FLTI)
	{
		switch (func)
		{
			case A_FLTIFUNC_ADDS:
			case A_FLTIFUNC_ADDS_C:
			case A_FLTIFUNC_ADDS_D:
			case A_FLTIFUNC_ADDS_M:
			case A_FLTIFUNC_ADDS_SU:
			case A_FLTIFUNC_ADDS_SUC:
			case A_FLTIFUNC_ADDS_SUD:
			case A_FLTIFUNC_ADDS_SUI:
			case A_FLTIFUNC_ADDS_SUIC:
			case A_FLTIFUNC_ADDS_SUIM:
			case A_FLTIFUNC_ADDS_SUM:
			case A_FLTIFUNC_ADDS_U:
			case A_FLTIFUNC_ADDS_UC:
			case A_FLTIFUNC_ADDS_UD:
			case A_FLTIFUNC_ADDS_UM:
				res.value.fromfloat(Rav.tofloat() + Rbv.tofloat());
				latency = m_config.addLatency;
				//DebugSimWrite("%f + %f = %f\n", Rav.tofloat(), Rbv.tofloat(), res.value.tofloat());
				break;

			case A_FLTIFUNC_SUBS:
			case A_FLTIFUNC_SUBS_C:
			case A_FLTIFUNC_SUBS_D:
			case A_FLTIFUNC_SUBS_M:
			case A_FLTIFUNC_SUBS_SU:
			case A_FLTIFUNC_SUBS_SUC:
			case A_FLTIFUNC_SUBS_SUD:
			case A_FLTIFUNC_SUBS_SUI:
			case A_FLTIFUNC_SUBS_SUIC:
			case A_FLTIFUNC_SUBS_SUIM:
			case A_FLTIFUNC_SUBS_SUM:
			case A_FLTIFUNC_SUBS_U:
			case A_FLTIFUNC_SUBS_UC:
			case A_FLTIFUNC_SUBS_UD:
			case A_FLTIFUNC_SUBS_UM:
				res.value.fromfloat(Rav.tofloat() - Rbv.tofloat());
				latency = m_config.subLatency;
				//DebugSimWrite("%f - %f = %f\n", Rav.tofloat(), Rbv.tofloat(), res.value.tofloat());
				break;

			case A_FLTIFUNC_MULS:
			case A_FLTIFUNC_MULS_C:
			case A_FLTIFUNC_MULS_D:
			case A_FLTIFUNC_MULS_M:
			case A_FLTIFUNC_MULS_SU:
			case A_FLTIFUNC_MULS_SUC:
			case A_FLTIFUNC_MULS_SUD:
			case A_FLTIFUNC_MULS_SUI:
			case A_FLTIFUNC_MULS_SUIC:
			case A_FLTIFUNC_MULS_SUIM:
			case A_FLTIFUNC_MULS_SUM:
			case A_FLTIFUNC_MULS_U:
			case A_FLTIFUNC_MULS_UC:
			case A_FLTIFUNC_MULS_UD:
			case A_FLTIFUNC_MULS_UM:
				res.value.fromfloat(Rav.tofloat() * Rbv.tofloat());
				latency = m_config.mulLatency;
				//DebugSimWrite("%f * %f = %f\n", Rav.tofloat(), Rbv.tofloat(), res.value.tofloat());
				break;

			case A_FLTIFUNC_DIVS:
			case A_FLTIFUNC_DIVS_C:
			case A_FLTIFUNC_DIVS_D:
			case A_FLTIFUNC_DIVS_M:
			case A_FLTIFUNC_DIVS_SU:
			case A_FLTIFUNC_DIVS_SUC:
			case A_FLTIFUNC_DIVS_SUD:
			case A_FLTIFUNC_DIVS_SUI:
			case A_FLTIFUNC_DIVS_SUIC:
			case A_FLTIFUNC_DIVS_SUIM:
			case A_FLTIFUNC_DIVS_SUM:
			case A_FLTIFUNC_DIVS_U:
			case A_FLTIFUNC_DIVS_UC:
			case A_FLTIFUNC_DIVS_UD:
			case A_FLTIFUNC_DIVS_UM:
				res.value.fromfloat(Rav.tofloat() / Rbv.tofloat());
				latency = m_config.divLatency;
				//DebugSimWrite("%f / %f = %f\n", Rav.tofloat(), Rbv.tofloat(), res.value.tofloat());
				break;

			case A_FLTIFUNC_ADDT:
			case A_FLTIFUNC_ADDT_C:
			case A_FLTIFUNC_ADDT_D:
			case A_FLTIFUNC_ADDT_M:
			case A_FLTIFUNC_ADDT_SU:
			case A_FLTIFUNC_ADDT_SUC:
			case A_FLTIFUNC_ADDT_SUD:
			case A_FLTIFUNC_ADDT_SUI:
			case A_FLTIFUNC_ADDT_SUIC:
			case A_FLTIFUNC_ADDT_SUIM:
			case A_FLTIFUNC_ADDT_SUM:
			case A_FLTIFUNC_ADDT_U:
			case A_FLTIFUNC_ADDT_UC:
			case A_FLTIFUNC_ADDT_UD:
			case A_FLTIFUNC_ADDT_UM:
				res.value.fromdouble(Rav.todouble() + Rbv.todouble());
				latency = m_config.addLatency;
				//DebugSimWrite("%lf + %lf = %lf\n", Rav.todouble(), Rbv.todouble(), res.value.todouble());
				break;

			case A_FLTIFUNC_SUBT:
			case A_FLTIFUNC_SUBT_C:
			case A_FLTIFUNC_SUBT_D:
			case A_FLTIFUNC_SUBT_M:
			case A_FLTIFUNC_SUBT_SU:
			case A_FLTIFUNC_SUBT_SUC:
			case A_FLTIFUNC_SUBT_SUD:
			case A_FLTIFUNC_SUBT_SUI:
			case A_FLTIFUNC_SUBT_SUIC:
			case A_FLTIFUNC_SUBT_SUIM:
			case A_FLTIFUNC_SUBT_SUM:
			case A_FLTIFUNC_SUBT_U:
			case A_FLTIFUNC_SUBT_UC:
			case A_FLTIFUNC_SUBT_UD:
			case A_FLTIFUNC_SUBT_UM:
				res.value.fromdouble(Rav.todouble() - Rbv.todouble());
				latency = m_config.subLatency;
				//DebugSimWrite("%lf - %lf = %lf\n", Rav.todouble(), Rbv.todouble(), res.value.todouble());
				break;

			case A_FLTIFUNC_MULT:
			case A_FLTIFUNC_MULT_C:
			case A_FLTIFUNC_MULT_D:
			case A_FLTIFUNC_MULT_M:
			case A_FLTIFUNC_MULT_SU:
			case A_FLTIFUNC_MULT_SUC:
			case A_FLTIFUNC_MULT_SUD:
			case A_FLTIFUNC_MULT_SUI:
			case A_FLTIFUNC_MULT_SUIC:
			case A_FLTIFUNC_MULT_SUIM:
			case A_FLTIFUNC_MULT_SUM:
			case A_FLTIFUNC_MULT_U:
			case A_FLTIFUNC_MULT_UC:
			case A_FLTIFUNC_MULT_UD:
			case A_FLTIFUNC_MULT_UM:
				res.value.fromdouble(Rav.todouble() * Rbv.todouble());
				latency = m_config.mulLatency;
				//DebugSimWrite("%lf * %lf = %lf\n", Rav.todouble(), Rbv.todouble(), res.value.todouble());
				break;

			case A_FLTIFUNC_DIVT:
			case A_FLTIFUNC_DIVT_C:
			case A_FLTIFUNC_DIVT_D:
			case A_FLTIFUNC_DIVT_M:
			case A_FLTIFUNC_DIVT_SU:
			case A_FLTIFUNC_DIVT_SUC:
			case A_FLTIFUNC_DIVT_SUD:
			case A_FLTIFUNC_DIVT_SUI:
			case A_FLTIFUNC_DIVT_SUIC:
			case A_FLTIFUNC_DIVT_SUIM:
			case A_FLTIFUNC_DIVT_SUM:
			case A_FLTIFUNC_DIVT_U:
			case A_FLTIFUNC_DIVT_UC:
			case A_FLTIFUNC_DIVT_UD:
			case A_FLTIFUNC_DIVT_UM:
				res.value.fromdouble(Rav.todouble() / Rbv.todouble());
				latency = m_config.divLatency;
				//DebugSimWrite("%lf / %lf = %lf\n", Rav.todouble(), Rbv.todouble(), res.value.todouble());
				break;
		}
	}
	res.address    = Rc;
	res.completion = getKernel()->getCycleNo() + latency;

	if (!m_pipelines[latency].empty() && m_pipelines[latency].front().completion == res.completion)
	{
		// The pipeline is full (because of a stall)
		return false;
	}

	//DebugSimWrite("Queued operation. Done at %lld\n", res.completion);
	COMMIT{ m_pipelines[latency].push_back(res); }
	return true;
}

bool FPU::onCompletion(const Result& res) const
{
	if (!m_registerFile.p_asyncW.write(*this, res.address))
	{
		return false;
	}

	RegValue value;
	if (!m_registerFile.readRegister(res.address, value))
	{
		return false;
	}

	if (value.m_state != RST_PENDING && value.m_state != RST_WAITING)
	{
		// We're too fast, wait!
		return false;
	}

	value.m_state = RST_FULL;
	value.m_float = res.value;
	if (!m_registerFile.writeRegister(res.address, value, *this))
	{
		return false;
	}

	return true;
}

Result FPU::onCycleWritePhase(int stateIndex)
{
	CycleNo now = getKernel()->getCycleNo();
	for (map<CycleNo, deque<Result> >::iterator p = m_pipelines.begin(); p != m_pipelines.end(); p++)
	{
		Result& res = p->second.front();
		if (res.completion <= now)
		{
			// Write back result
			if (!onCompletion(res))
			{
				// Stall pipeline
				COMMIT
				{
					for (deque<Result>::iterator q = p->second.begin(); q != p->second.end(); q++)
					{
						q->completion++;
					}
				}
				return FAILED;
			}

			COMMIT
			{
				// Remove from queue
				p->second.pop_front();
				if (p->second.empty())
				{
					m_pipelines.erase(p);
				}
			}
			return SUCCESS;
		}
	}
	return m_pipelines.empty() ? DELAYED : SUCCESS;
}

FPU::FPU(Processor& parent, const std::string& name, RegisterFile& regFile, const Config& config)
	: IComponent(&parent, parent.getKernel(), name), m_registerFile(regFile), m_config(config)
{
}

}
