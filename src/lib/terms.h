/*

 Copyright (c) 2006-2010, The Scripps Research Institute

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

 Author: Dr. Oleg Trott <ot14@columbia.edu>,
 The Olson Lab,
 The Scripps Research Institute

 */

#ifndef VINA_TERMS_H
#define VINA_TERMS_H

#include <boost/ptr_container/ptr_vector.hpp> 
#include "model.h"

struct term
{
	std::string name;
	virtual ~term()
	{
	}
};

enum TermComponent
{
	TypeDependentOnly, //not charge dependent, can be caches based on simple atom type
};
struct distance_additive: public term
{
	fl cutoff;
	distance_additive(fl cutoff_) :
			cutoff(cutoff_)
	{
	}
	virtual fl eval(const atom_base& a, const atom_base& b, fl r) const = 0;
	virtual ~distance_additive()
	{
	}
};

struct charge_dependent: public distance_additive
{
	//a charge dependent term must be separated into components
	//that are dependent on different charges to enable precalculation
	struct components
	{
		fl type_dependent_only; //no need to adjust by charge
		fl a_charge_dependent; //multiply by a's charge
		fl b_charge_dependent; //multiply by b's charge
		fl ab_charge_dependent; //multiply by a*b

		//add in rhs
		components& operator+=(const components& rhs)
		{
			type_dependent_only += rhs.type_dependent_only;
			a_charge_dependent += rhs.a_charge_dependent;
			b_charge_dependent += rhs.b_charge_dependent;
			ab_charge_dependent += rhs.ab_charge_dependent;
			return *this;
		}

		//add in a non-charge dependent term
		components& operator+=(fl val)
		{
			type_dependent_only += val;
			return *this;
		}
	};

	//unique to charge_dependent, return comonents for given types and distance
	virtual components eval_components(smt t1, smt t2, fl r) const = 0;

	fl eval(const atom_base& a, const atom_base& b, fl r) const
	{
		components c = eval_components(a.sm, b.sm, r);
		return c.type_dependent_only +
				a.charge*c.a_charge_dependent +
				b.charge*c.b_charge_dependent +
				a.charge*b.charge*c.ab_charge_dependent;
	}
};

struct usable: public distance_additive
{
	usable(fl cutoff_) :
			distance_additive(cutoff_)
	{
	}
	fl eval(const atom_base& a, const atom_base& b, fl r) const
	{
		return eval(a.get(), b.get(), r);
	}
	virtual fl eval(smt t1, smt t2, fl r) const
	{
		VINA_CHECK(false);
		return 0;
	}
	virtual ~usable()
	{
	}
};

struct additive: public term
{
	fl cutoff;
	additive() :
			cutoff(max_fl)
	{
	}
	virtual fl eval(const model& m, const atom_index& i,
			const atom_index& j) const = 0;
	virtual ~additive()
	{
	}
};

struct intermolecular: public term
{
	virtual fl eval(const model& m) const = 0;
};

struct conf_independent_inputs
{
	fl num_tors;
	fl num_rotors;
	fl num_heavy_atoms;
	fl num_hydrophobic_atoms;
	fl ligand_max_num_h_bonds;
	fl num_ligands;
	fl ligand_lengths_sum;
	operator flv() const;
	conf_independent_inputs(const model& m);
	std::vector<std::string> get_names() const;
	conf_independent_inputs();
	private:
	unsigned num_bonded_heavy_atoms(const model& m, const atom_index& i) const; // FIXME? - could be static, but I don't feel like declaring function friends
	unsigned atom_rotors(const model& m, const atom_index& i) const; // the number of rotatable bonds to heavy ligand atoms

	friend class boost::serialization::access;
	template<class Archive>
	void serialize(Archive & ar, const unsigned version)
	{
		ar & num_tors;
		ar & num_rotors;
		ar & num_heavy_atoms;
		ar & num_hydrophobic_atoms;
		ar & ligand_max_num_h_bonds;
		ar & num_ligands;
		ar & ligand_lengths_sum;
	}
};

struct conf_independent: public term
{
	virtual fl eval(const conf_independent_inputs& in, fl x,
			flv::const_iterator& it) const = 0;
	virtual sz size() const = 0; // how many parameters does it take
};

template<typename T>
struct term_set
{
	std::vector<bool> enabled;
	boost::ptr_vector<T> fun; // FIXME? const T?
	void add(unsigned e, T* f)
	{ // FIXME? const T* ?
		enabled.push_back(e > 0);
		fun.push_back(f);
	}
	sz num_enabled() const
	{
		sz tmp = 0;
		VINA_FOR_IN(i, enabled)
		if(enabled[i])
		++tmp;
		return tmp;
	}
	void get_names(bool enabled_only, std::vector<std::string>& out) const
	{ // appends to "out"
		VINA_CHECK(enabled.size() == fun.size());
		VINA_FOR_IN(i, fun)
		if(!enabled_only || enabled[i])
		out.push_back(fun[i].name);
	}
	void filter(flv::const_iterator& in, flv& out) const
	{
		VINA_CHECK(enabled.size() == fun.size());
		VINA_FOR_IN(i, enabled)
		{
			if(enabled[i])
			out.push_back(*in);
			++in;
		}
	}
	fl max_cutoff() const
	{
		fl tmp = 0;
		VINA_FOR_IN(i, fun)
		tmp = (std::max)(tmp, fun[i].cutoff);
		return tmp;
	}
	sz size() const
	{	return fun.size();}
	const T& operator[](sz i) const
	{	return fun[i];}
};

struct factors
{
	flv e; // external
	flv i; // internal
	sz size() const
	{
		return e.size() + i.size();
	}
	//sz num_weights() const { return (std::max)(e.size(), i.size()); } // FIXME? compiler bug? getting warnings here
	sz num_weights() const
	{
		return (e.size() > i.size()) ? e.size() : i.size();
	}
	fl eval(const flv& weights, bool include_internal) const;
	private:
	friend class boost::serialization::access;
	template<class Archive>
	void serialize(Archive & ar, const unsigned version)
	{
		ar & e;
		ar & i;
	}
};

struct terms
{
	term_set<distance_additive> distance_additive_terms;
	term_set<usable> usable_terms;
	term_set<additive> additive_terms;
	term_set<intermolecular> intermolecular_terms;
	term_set<conf_independent> conf_independent_terms;

	// the class takes ownership of the pointer with 'add'
	void add(unsigned e, distance_additive* p)
	{
		distance_additive_terms.add(e, p);
	}
	void add(unsigned e, usable* p)
	{
		usable_terms.add(e, p);
	}
	void add(unsigned e, additive* p)
	{
		additive_terms.add(e, p);
	}
	void add(unsigned e, intermolecular* p)
	{
		intermolecular_terms.add(e, p);
	}
	void add(unsigned e, conf_independent* p)
	{
		conf_independent_terms.add(e, p);
	}

	std::vector<std::string> get_names(bool enabled_only) const; // does not include conf-independent
	sz size_internal() const;
	sz size() const
	{
		return size_internal() + intermolecular_terms.size();
	}
	sz size_conf_independent(bool enabled_only) const; // number of parameters does not necessarily equal the number of operators
	fl max_r_cutoff() const;
	flv evale_robust(const model& m) const;
	fl eval_conf_independent(const conf_independent_inputs& in, fl x,
			flv::const_iterator& it) const;
	flv filter_external(const flv& v) const;
	flv filter_internal(const flv& v) const;
	factors filter(const factors& f) const;
	void display_info() const;
	private:
	void eval_additive_aux(const model& m, const atom_index& i,
			const atom_index& j, fl r, flv& out) const; // out is added to

};

#endif