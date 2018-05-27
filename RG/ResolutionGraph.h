/*
 * ResolutionGraph.h
 *
 *  Created on: May 27, 2018
 *      Author: nadeem & shadi :)
 */

#ifndef RESOLUTIONGRAPH_H_
#define RESOLUTIONGRAPH_H_

#include "RGraphTypes/Alloc.h"
#include "RGraphTypes/Vec.h"
#include "RGraphTypes/Alg.h"
#include <string.h>

namespace RGraph {

typedef uint32_t RRef; //RRef - Resolution Ref
const RRef RRef_Undef = UINT32_MAX;

//================================================Resol Class========================================================//
class Resol
{
#define IC_OFFSET 1
#define REM_OFFSET 1
public:

	//FIXME do we need another flag which will act like a switch: if 1 then store only icParents,
	//if 0 also store remParents?

	//stores the uids of the node's children (unlike parents children nodes are added dynamically)
    vec<uint32_t> children;

	struct {
		unsigned ic : 1; //FIXME 1 if ic, 0 if remainder?.
		unsigned hasRemParents : 1; //if this flag is turned on, then this node contains some remainder clauses as parents (in parents below).
		unsigned nRefCount : 30; // the number of nodes (this node + children) known to be ic by this node. Basically the count of all the ic children and (if this node is ic) itself.

	} header; //take up 1 32-bit words in memory

    /*
    how parents array looks in action:
    |icParents_size||ic_parent|......|remParents_size||rem_parent|.....|flags|
    */
    union {
		uint32_t icSize;	// number of parents who are ic.
        uint32_t icParent; // uid of parents who are ic.
		uint32_t remSize; //number of parents who are remainder clauses .
		uint32_t remParent; // uid of parents who are remainder clauses.
		uint32_t guideFlags; //an ordering  guide (implemented as a boolean array packed to uint32_t array) to the location of each parent (ic or rem) in the node - 0 means to look for it in the rem part - while 1 means to look for the ic part. This array tracks to total ordering of the parents as it was determined in creating the node (the order of the parents during the analyze conflict call is the order kept here)
    } parents[0];

    //returns the address of the first icParent.
    //FIXME why the method won't compile if we add const annotation?.
    uint32_t* IcParents() {
       return &parents[IC_OFFSET].icParent;
    }

    //returns the address of the first remParent if existed else NULL.
	uint32_t* RemParents() {
		return (header.hasRemParents) ? &parents[IC_OFFSET + icParentsSize() + REM_OFFSET].remParent : NULL;
	}

	//returns the number of the icParents.
    inline int icParentsSize() const {
        return parents[0].icSize;
    }

    //returns the number of the remParents.
	inline int remParentsSize() const {
		return (!header.hasRemParents) ? 0 : parents[IC_OFFSET + icParentsSize()].remSize;
	}

	//returns the size of flags + 1 for remParents_size cell (all done in 32 bits units).
	inline int additionalDataSize() const {
		if (!header.hasRemParents)
			return 0;
		int totalParentsNum = icParentsSize() + remParentsSize();

		//the +1 is for the cell holding the number of remainders.
		//all the rest are for the size of the flag array
		return  1 + (totalParentsNum / 32) + (int)((totalParentsNum % 32) != 0);
	}

	//returns the size of resol in 32bits units.
    uint32_t sizeIn32Bit() const {
		int totalParentsNum = icParentsSize() + remParentsSize();
        return SIZE + totalParentsNum + additionalDataSize();
    }

    //returns the number of total parents.
	uint32_t size() const {
		return icParentsSize() + remParentsSize();
	}

    friend class ResolAllocator;

    /* sizeof(vec<unit32_t>)>>2 -> divide by 4 to convert to 32bit unit.
     * +2 -> icParents_size cell +  header.
     */
    static const uint32_t SIZE = (sizeof(vec<uint32_t>) >> 2) + 2;

    Resol(const vec<Uid>& icParents,const vec<Uid>& remParents, const vec<Uid>& allParents,bool ic){
		assert(icParents.size() + remParents.size() == allParents.size());
		header.ic = (int)ic;
		header.nRefCount = 1;

        parents[0].icSize = icParents.size();
		uint32_t* ics = &(parents[IC_OFFSET].icParent);
		if (remParents.size() == 0) {
			header.hasRemParents = 0;
			for (int i = 0; i < icParents.size(); ++i) {
				ics[i] = icParents[i];
			}
		}
		else { // remParents.size() > 0
			header.hasRemParents = 1;
			parents[IC_OFFSET + icParents.size()].remSize = remParents.size();
			uint32_t * rems = &(parents[IC_OFFSET + icParents.size() + REM_OFFSET].remParent);
			uint32_t * flags = &(parents[IC_OFFSET + icParents.size() + REM_OFFSET + remParentsSize()].guideFlags);
			uint32_t i = 0, j = 0;
			BitArray ba = BitArray(flags);
			for (uint32_t idx = 0; idx < allParents.size(); ++idx) {
				if (icParents[i] == allParents[idx]) {
					ics[i++] = allParents[idx];
					ba.addBitMSB(1);
				}
				else {
					assert(allParents[idx] == remParents[j]);
					rems[j++] = allParents[idx];
					ba.addBitMSB(0);
				}
			}
		}
    }

	class ParentIter {
		const Resol& r;
		const uint32_t size;
		int idx;
		int icIdx;
		int remIdx;

		const uint32_t* flags;
		uint32_t f_word;
		uint32_t f_mask;

		void initFlags() {
			//flags is an array of uint32_t, given idx of the node we can find the right bit in flags by:
			f_mask = 1 << (idx % 32); //the right bit in f_word
			f_word = flags[idx / 32]; //the index of the right cell in flags array.
		}

		//f_mask is a 32-bit word with a single bit on, incrementing it will bit-shift the bit towards the MSB. If overflowing, rotate back to LSB.
		void incFlagData() {
			if (f_mask < MAX_MASK)
				f_mask = f_mask << 1;
			else {
				f_mask = MIN_MASK;
				f_word = flags[idx / 32];
			}
		}

		//check new flag in flags array. If the new flag is 1 then the next parent is an ic, otherwise it is remainder. Increment the relevant new index accordingly.
		void updateIdxs() {
			if (!r.header.hasRemParents | (f_mask & f_word)) //if there was no remParents then (f_mask & f_word) = 0.
				icIdx++;
			else
				remIdx++;
		}

	public:
		ParentIter(const Resol& r, uint32_t idx = 0) :
			r(r),
			size(r.icParentsSize() + r.remParentsSize()),
			idx(idx),
			icIdx(-1),
			remIdx(-1),
			flags( (r.header.hasRemParents) ? &(r.parents[IC_OFFSET + r.icParentsSize() + REM_OFFSET + r.remParentsSize()].guideFlags) : NULL ){
			if (r.header.hasRemParents) {
				initFlags();
			}
			updateIdxs();
		}

		bool operator!=(const ParentIter& other) {
			return &r != &other.r || idx != other.idx;
		}

		const ParentIter& operator++() {
			idx++;
			if (idx > size)
				//if already at the end of the iterator, do nothing.
				idx = size;
			else {
				if (r.header.hasRemParents) {
					//remainder parents exist, use flag array to increment between ic and rem parents
					incFlagData();
				}
				updateIdxs();
			}
			return *this;
		}

		const uint32_t& operator*() const {
			if (idx >= size)
				return RRef_Undef;
			else if (r.header.hasRemParents && !(f_mask & f_word)) //next parent is a remainder parent
				return r.parents[IC_OFFSET + r.icParentsSize() + REM_OFFSET+ remIdx].remParent;
			else {
				return r.parents[IC_OFFSET + icIdx].icParent;
			}
		}
	};

	const ParentIter begin() const {
		return ParentIter(*this, 0);
	}

	const ParentIter end() const {
		return ParentIter(*this, icParentsSize() + remParentsSize());
	}
};

//================================================ResolAllocator Class========================================================//

class ResolAllocator : public RegionAllocator<uint32_t>
{
public:
	void updateAllocated(RRef rRef, const vec<uint32_t>& icParents, const vec<uint32_t>& remParents, const vec<uint32_t>& allParents) {
		new (lea(rRef)) Resol(icParents, remParents, allParents, operator[](rRef).header.ic);
	}
    RRef alloc(const vec<uint32_t>& icParents, const vec<uint32_t>& remParents, const vec<uint32_t>& allParents,bool ic)
    {

		int additionalSize = (remParents.size() == 0) ? 0 : (1 + remParents.size() + (allParents.size() / 32) + (int)((allParents.size() % 32) > 0));
		RRef rRef = RegionAllocator<uint32_t>::alloc(Resol::SIZE + icParents.size() + additionalSize);

        new (lea(rRef)) Resol(icParents,remParents,allParents,ic);

        return rRef;
    }

    // Deref, Load Effective Address (LEA), Inverse of LEA (AEL):
    Resol&       operator[](Ref r)       { return (Resol&)RegionAllocator<uint32_t>::operator[](r); }
    const Resol& operator[](Ref r) const { return (Resol&)RegionAllocator<uint32_t>::operator[](r); }
    Resol*       lea       (Ref r)       { return (Resol*)RegionAllocator<uint32_t>::lea(r); }
    const Resol* lea       (Ref r) const { return (Resol*)RegionAllocator<uint32_t>::lea(r); }
    Ref           ael       (const Resol* t){ return RegionAllocator<uint32_t>::ael((uint32_t*)t); }

    void free(RRef rref)
    {
        Resol& r = operator[](rref);
        r.children.clear(true);
        RegionAllocator<uint32_t>::free(r.sizeIn32Bit());
    }

    void StartReloc()
    {
        m_LastRelocLoc = 0;
    }

    void Reloc(CRef& from) //'from' is a resol ref
    {
        if (from == CRef_Undef)
            return;
        uint32_t size = operator[](from).sizeIn32Bit();

        if (from == m_LastRelocLoc) {  // the same clause no need to copy
            m_LastRelocLoc += size;
            return;
        }

        assert(from > m_LastRelocLoc);
        uint32_t* pFrom = RegionAllocator<uint32_t>::lea(from);
        uint32_t* pTo = RegionAllocator<uint32_t>::lea(m_LastRelocLoc);
        //memcpy(RegionAllocator<uint32_t>::lea(m_LastRelocLoc), , size);
        for (uint32_t nPart = 0; nPart < size; ++nPart) {
            *pTo = *pFrom;
            ++pTo;
            ++pFrom;
        }

        from = m_LastRelocLoc;
        m_LastRelocLoc += size;
    }

    void FinishReloc()
    {
        SetSize(m_LastRelocLoc);
        ClearWasted();
    }

private:
    CRef m_LastRelocLoc;
};

//================================================ResolutionGraph Class========================================================//

class ResolutionGraph
{
public:

    Resol& getResol(RRef ref) {
	 return ra[ref];
	}

	void addNewResolution(uint32_t nNewClauseId, CRef ref, const vec<Uid>& icParents);
    void addNewResolution(uint32_t nNewClauseId, CRef ref, const vec<Uid>& icParents, const vec<Uid>& remParents, const vec<Uid>& allParents);
	void addRemainderResolution(uint32_t nNewClauseId, CRef ref);
    void getClausesCones(vec<uint32_t>& cone);

    CRef getClauseRef(uint32_t nUid) const {
        return uidToData[nUid].clauseRef;
    }

    RRef GetResolRef(uint32_t nUid) {
        return uidToData[nUid].resolRef;
    }

    void deleteClause(uint32_t nUid) {
        DecreaseReference(nUid);
        uidToData[nUid].clauseRef = CRef_Undef;
    }

    void CheckGarbage() {
        if (ra.wasted() > ra.size() * 0.3)
            Shrink();
    }

    int getParentsNumber(uint32_t nUid){
        return getResol(GetResolRef(nUid)).icParentsSize();
    }

    bool isValidUid(uint32_t uid){
        return GetResolRef(uid) != RRef_Undef;
    }

    uint32_t getMaxUid() const{
        return uidToData.size();
    }

    //FIXME what the fuck is this? anekak???? Set<uint32_t> m_icPoEC;
    //std::unordered_map<uint32_t, vec<Lit>*>  icDelayedRemoval;

	struct Pair
    {
        CRef clauseRef;
        CRef resolRef;

        Pair() : clauseRef(CRef_Undef) , resolRef(CRef_Undef)
        {}
    };

    // Map that contains mapping between clause uid to its index in clause buffer
	//the index of the vec is the uid and the data is Pair of clauseRef and resolRef
    vec<Pair> uidToData;
private:
    void DecreaseReference(uint32_t nUid);

    void Shrink();

    ResolAllocator ra;
    vec<uint32_t> dummy;
};

}
#endif /* RESOLUTIONGRAPH_H_ */
