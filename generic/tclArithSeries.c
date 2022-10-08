/*
 * tclArithSeries.c --
 *
 *     This file contains the ArithSeries concrete abstract list
 *     implementation. It implements the inner workings of the lseq command.
 *
 * Copyright © 2022 Brian S. Griffin.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <assert.h>
#include "tcl.h"
#include "tclInt.h"
#include "tclArithSeries.h"


/*
 * The structure below defines the arithmetic series Tcl Obj Type by means of
 * procedures that can be invoked by generic object code.
 *
 * The arithmetic series object is a Tcl_AbstractList representing an interval
 * of an arithmetic series in constant space.
 *
 * The arithmetic series is internally represented with three integers,
 * *start*, *end*, and *step*, Where the length is calculated with
 * the following algorithm:
 *
 * if RANGE == 0 THEN
 *   ERROR
 * if RANGE > 0
 *   LEN is (((END-START)-1)/STEP) + 1
 * else if RANGE < 0
 *   LEN is (((END-START)-1)/STEP) - 1
 *
 * And where the list's I-th element is calculated
 * as:
 *
 * LIST[i] = START+(STEP*i)
 *
 * Zero elements ranges, like in the case of START=10 END=10 STEP=1
 * are valid and will be equivalent to the empty list.
 */

#define ArithSeriesIndexM(arithSeriesRepPtr, index) \
    ((arithSeriesRepPtr)->isDouble ?					\
     (((ArithSeriesDbl*)(arithSeriesRepPtr))->start+((index) * ((ArithSeriesDbl*)(arithSeriesRepPtr))->step)) \
     :									\
     ((arithSeriesRepPtr)->start+((index) * arithSeriesRepPtr->step)))

static int TclArithSeriesObjStep(Tcl_Obj *arithSeriesPtr, Tcl_Obj **stepObj);
static int TclArithSeriesObjIndex(Tcl_Obj *arithSeriesPtr,
                            Tcl_WideInt index, Tcl_Obj **elemObj);
static Tcl_WideInt TclArithSeriesObjLength(Tcl_Obj *arithSeriesObj);
static Tcl_Obj *TclArithSeriesObjRange(Tcl_Obj *arithSeriesPtr,
			    Tcl_WideInt fromIdx, Tcl_WideInt toIdx);
static Tcl_Obj *TclArithSeriesObjReverse(Tcl_Obj *arithSeriesPtr);
static int TclArithSeriesGetElements(Tcl_Interp *interp,
			    Tcl_Obj *objPtr, int *objcPtr, Tcl_Obj ***objvPtr);
static Tcl_Obj *TclNewArithSeriesInt(Tcl_WideInt start,
			    Tcl_WideInt end, Tcl_WideInt step,
			    Tcl_WideInt len);
static Tcl_Obj *TclNewArithSeriesDbl(double start, double end,
			    double step, Tcl_WideInt len);
static void DupArithSeriesRep(Tcl_Obj *srcPtr, Tcl_Obj *copyPtr);
static void FreeArithSeriesRep(Tcl_Obj *arithSeriesObjPtr);
static void UpdateStringOfArithSeries(Tcl_Obj *arithSeriesObjPtr);
static Tcl_Obj *Tcl_NewArithSeriesObj(int objc, Tcl_Obj *objv[]);

static Tcl_AbstractListType arithSeriesType = {
	TCL_ABSTRACTLIST_VERSION_1,
	"arithseries",
	Tcl_NewArithSeriesObj,
	DupArithSeriesRep,
	TclArithSeriesObjLength,
	TclArithSeriesObjIndex,
	TclArithSeriesObjRange,
	TclArithSeriesObjReverse,
        TclArithSeriesGetElements,
        FreeArithSeriesRep,
	UpdateStringOfArithSeries
};

/*
 *----------------------------------------------------------------------
 *
 * Arithserieslen --
 *
 * 	Compute the length of the equivalent list where
 * 	every element is generated starting from *start*,
 * 	and adding *step* to generate every successive element
 * 	that's < *end* for positive steps, or > *end* for negative
 * 	steps.
 *
 * Results:
 *
 * 	The length of the list generated by the given range,
 * 	that may be zero.
 * 	The function returns -1 if the list is of length infiite.
 *
 * Side effects:
 *
 * 	None.
 *
 *----------------------------------------------------------------------
 */
static Tcl_WideInt
ArithSeriesLen(Tcl_WideInt start, Tcl_WideInt end, Tcl_WideInt step)
{
    Tcl_WideInt len;

    if (step == 0) return 0;
    len = (step ? (1 + (((end-start))/step)) : 0);
    return (len < 0) ? -1 : len;
}

/*
 *----------------------------------------------------------------------
 *
 * DupArithSeriesRep --
 *
 *	Initialize the internal representation of a ArithSeries abstract list
 *	Tcl_Obj to a copy of the internal representation of an existing
 *	arithseries object.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	We set "copyPtr"s internal rep to a pointer to a
 *	newly allocated AbstractList structure.
 *----------------------------------------------------------------------
 */

static void
DupArithSeriesRep(Tcl_Obj *srcPtr, Tcl_Obj *copyPtr)
{
    ArithSeries *srcArithSeries = (ArithSeries*)Tcl_AbstractListGetConcreteRep(srcPtr);
    ArithSeries *copyArithSeries = (ArithSeries *)ckalloc(sizeof(ArithSeries));

    *copyArithSeries = *srcArithSeries;

    /* Note: we do not have to be worry about existing internal rep because
       copyPtr is supposed to be freshly initialized */
    Tcl_AbstractListSetConcreteRep(copyPtr, copyArithSeries);
}


/*
 *----------------------------------------------------------------------
 *
 * FreeArithSeriesRep --
 *
 *	Free any allocated memory in the ArithSeries Rep
 *
 * Results:
 *	None.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static void
FreeArithSeriesRep(Tcl_Obj *arithSeriesObjPtr)  /* Free any allocated memory */
{
    ArithSeries *arithSeriesPtr = (ArithSeries*)Tcl_AbstractListGetConcreteRep(arithSeriesObjPtr);
    if (arithSeriesPtr) {
	if (arithSeriesPtr->elements) {
	    Tcl_WideInt i, len = arithSeriesPtr->len;
	    for (i=0; i<len; i++) {
		Tcl_DecrRefCount(arithSeriesPtr->elements[i]);
	    }
	    ckfree((char*)arithSeriesPtr->elements);
	    arithSeriesPtr->elements = NULL;
	}
	ckfree((char*)arithSeriesPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TclNewArithSeriesInt --
 *
 *	Creates a new ArithSeries object. The returned object has
 *	refcount = 0.
 *
 * Results:
 *
 * 	A Tcl_Obj pointer to the created ArithSeries object.
 * 	A NULL pointer of the range is invalid.
 *
 * Side Effects:
 *
 * 	None.
 *----------------------------------------------------------------------
 */
Tcl_Obj *
TclNewArithSeriesInt(Tcl_WideInt start, Tcl_WideInt end, Tcl_WideInt step, Tcl_WideInt len)
{
    Tcl_WideInt length = (len>=0 ? len : ArithSeriesLen(start, end, step));
    Tcl_Obj *arithSeriesObj;
    ArithSeries *arithSeriesRepPtr;

    if (length <= 0) {
	TclNewObj(arithSeriesObj);
	return arithSeriesObj;
    }

    arithSeriesRepPtr = (ArithSeries*) ckalloc(sizeof (ArithSeries));
    arithSeriesRepPtr->isDouble = 0;
    arithSeriesRepPtr->start = start;
    arithSeriesRepPtr->end = end;
    arithSeriesRepPtr->step = step;
    arithSeriesRepPtr->len = length;
    arithSeriesRepPtr->elements = NULL;

    arithSeriesObj = Tcl_NewAbstractListObj(NULL, &arithSeriesType);
    Tcl_AbstractListSetConcreteRep(arithSeriesObj, arithSeriesRepPtr);

    if (length > 0)
    	Tcl_InvalidateStringRep(arithSeriesObj);

    return arithSeriesObj;
}

/*
 *----------------------------------------------------------------------
 *
 * TclNewArithSeriesDbl --
 *
 *	Creates a new ArithSeries object with doubles. The returned object has
 *	refcount = 0.
 *
 * Results:
 *
 * 	A Tcl_Obj pointer to the created ArithSeries object.
 * 	A NULL pointer of the range is invalid.
 *
 * Side Effects:
 *
 * 	None.
 *----------------------------------------------------------------------
 */
Tcl_Obj *
TclNewArithSeriesDbl(double start, double end, double step, Tcl_WideInt len)
{
    Tcl_WideInt length = (len>=0 ? len : ArithSeriesLen(start, end, step));
    Tcl_Obj *arithSeriesObj;
    ArithSeriesDbl *arithSeriesRepPtr;

    if (length <= 0) {
	TclNewObj(arithSeriesObj);
	return arithSeriesObj;
    }

    arithSeriesRepPtr = (ArithSeriesDbl*) ckalloc(sizeof (ArithSeriesDbl));
    arithSeriesRepPtr->isDouble = 1;
    arithSeriesRepPtr->start = start;
    arithSeriesRepPtr->end = end;
    arithSeriesRepPtr->step = step;
    arithSeriesRepPtr->len = length;
    arithSeriesRepPtr->elements = NULL;

    arithSeriesObj = Tcl_NewAbstractListObj(NULL, &arithSeriesType);
    Tcl_AbstractListSetConcreteRep(arithSeriesObj, arithSeriesRepPtr);

    if (length > 0)
    	Tcl_InvalidateStringRep(arithSeriesObj);

    return arithSeriesObj;
}

/*
 *----------------------------------------------------------------------
 *
 * assignNumber --
 *
 *	Create the approprite Tcl_Obj value for the given numeric values.
 *      Used locally only for decoding [lseq] numeric arguments.
 *	refcount = 0.
 *
 * Results:
 *
 * 	A Tcl_Obj pointer.
 *      No assignment on error.
 *
 * Side Effects:
 *
 * 	None.
 *----------------------------------------------------------------------
 */
static void
assignNumber(
    int useDoubles,
    Tcl_WideInt *intNumberPtr,
    double *dblNumberPtr,
    Tcl_Obj *numberObj)
{
    union {
	double d;
	Tcl_WideInt i;
    } *number;
    int tcl_number_type;

    if (TclGetNumberFromObj(NULL, numberObj, (ClientData*)&number, &tcl_number_type) != TCL_OK) {
	return;
    }
    if (useDoubles) {
	if (tcl_number_type == TCL_NUMBER_DOUBLE) {
	    *dblNumberPtr = number->d;
	} else {
	    *dblNumberPtr = (double)number->i;
	}
    } else {
	if (tcl_number_type == TCL_NUMBER_INT) {
	    *intNumberPtr = number->i;
	} else {
	    *intNumberPtr = (Tcl_WideInt)number->d;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclNewArithSeriesObj --
 *
 *	Creates a new ArithSeries object. Some arguments may be NULL and will
 *	be computed based on the other given arguments.
 *      refcount = 0.
 *
 * Results:
 *
 * 	A Tcl_Obj pointer to the created ArithSeries object.
 * 	An empty Tcl_Obj if the range is invalid.
 *
 * Side Effects:
 *
 * 	None.
 *----------------------------------------------------------------------
 */

int
TclNewArithSeriesObj(
    Tcl_Interp *interp,        /* For error reporting */
    Tcl_Obj **arithSeriesObj,  /* return value */
    int useDoubles,            /* Promote values to double when true,
                                * int otherwise */
    Tcl_Obj *startObj,         /* First value in list */
    Tcl_Obj *endObj,           /* Upper bound value of list */
    Tcl_Obj *stepObj,          /* Increment amount */
    Tcl_Obj *lenObj)           /* Number of elements */
{
    double dstart, dend, dstep;
    Tcl_WideInt start, end, step, len;

    if (startObj) {
	assignNumber(useDoubles, &start, &dstart, startObj);
    } else {
	start = 0;
	dstart = start;
    }
    if (stepObj) {
	assignNumber(useDoubles, &step, &dstep, stepObj);
	if (useDoubles) {
	    step = dstep;
	} else {
	    dstep = step;
	}
	if (dstep == 0) {
	    *arithSeriesObj = Tcl_NewObj();
            return TCL_OK;
	}
    }
    if (endObj) {
	assignNumber(useDoubles, &end, &dend, endObj);
    }
    if (lenObj) {
	if (TCL_OK != Tcl_GetWideIntFromObj(interp, lenObj, &len)) {
	    return TCL_ERROR;
	}
    }

    if (startObj && endObj) {
	if (!stepObj) {
	    if (useDoubles) {
		dstep = (dstart < dend) ? 1.0 : -1.0;
		step = dstep;
	    } else {
		step = (start < end) ? 1 : -1;
		dstep = step;
	    }
	}
	assert(dstep!=0);
	if (!lenObj) {
	    if (useDoubles) {
		len = (dend - dstart + dstep)/dstep;
	    } else {
		len = (end - start + step)/step;
	    }
	}
    }

    if (!endObj) {
	if (useDoubles) {
	    dend = dstart + (dstep * (len-1));
	    end = dend;
	} else {
	    end = start + (step * (len-1));
	    dend = end;
	}
    }

    if (TCL_MAJOR_VERSION < 9 && len > ListSizeT_MAX) {
	Tcl_SetObjResult(
	    interp,
	    Tcl_NewStringObj("max length of a Tcl list exceeded", -1));
	Tcl_SetErrorCode(interp, "TCL", "MEMORY", NULL);
	return TCL_ERROR;
    }

    if (arithSeriesObj) {
	*arithSeriesObj = (useDoubles)
	    ? TclNewArithSeriesDbl(dstart, dend, dstep, len)
	    : TclNewArithSeriesInt(start, end, step, len);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclArithSeriesObjLength
 *
 *	Returns the length of the arithmentic series.
 *
 * Results:
 *
 * 	The length of the series as Tcl_WideInt.
 *
 * Side Effects:
 *
 * 	None.
 *
 *----------------------------------------------------------------------
 */
Tcl_WideInt TclArithSeriesObjLength(Tcl_Obj *arithSeriesObj)
{
    ArithSeries *arithSeriesRepPtr = (ArithSeries *)Tcl_AbstractListGetConcreteRep(arithSeriesObj);
    return arithSeriesRepPtr->len;
}

/*
 *----------------------------------------------------------------------
 *
 * TclArithSeriesObjIndex --
 *
 *	Returns the element with the specified index in the list
 *	represented by the specified Arithmentic Sequence object.
 *	If the index is out of range, TCL_ERROR is returned,
 *	otherwise TCL_OK is returned and the integer value of the
 *	element is stored in *element.
 *
 * Results:
 *
 * 	TCL_OK on succes, TCL_ERROR on index out of range.
 *
 * Side Effects:
 *
 * 	On success, the integer pointed by *element is modified.
 *
 *----------------------------------------------------------------------
 */

int
TclArithSeriesObjIndex(Tcl_Obj *arithSeriesPtr, Tcl_WideInt index, Tcl_Obj **elemObj)
{
    ArithSeries *arithSeriesRepPtr;

    if (arithSeriesPtr->typePtr != &tclAbstractListType) {
	Tcl_Panic("TclArithSeriesObjIndex called with a not ArithSeries Obj.");
    }
    arithSeriesRepPtr = (ArithSeries *)Tcl_AbstractListGetConcreteRep(arithSeriesPtr);
    if (index < 0 || index >= arithSeriesRepPtr->len) {
        // TODO: need error message here
	return TCL_ERROR;
    }
    /* List[i] = Start + (Step * index) */
    if (arithSeriesRepPtr->isDouble) {
	*elemObj = Tcl_NewDoubleObj(ArithSeriesIndexM(arithSeriesRepPtr, index));
    } else {
	*elemObj = Tcl_NewWideIntObj(ArithSeriesIndexM(arithSeriesRepPtr, index));
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclArithSeriesObjStep --
 *
 *	Return a Tcl_Obj with the step value from the give ArithSeries Obj.
 *	refcount = 0.
 *
 * Results:
 *
 * 	A Tcl_Obj pointer to the created ArithSeries object.
 * 	A NULL pointer of the range is invalid.
 *
 * Side Effects:
 *
 * 	None.
 *----------------------------------------------------------------------
 */

int
TclArithSeriesObjStep(
    Tcl_Obj *arithSeriesPtr,
    Tcl_Obj **stepObj)
{
    ArithSeries *arithSeriesRepPtr;

    if (arithSeriesPtr->typePtr != &tclAbstractListType) {
        Tcl_Panic("TclArithSeriesObjIndex called with a not ArithSeries Obj.");
    }
    arithSeriesRepPtr = (ArithSeries *)Tcl_AbstractListGetConcreteRep(arithSeriesPtr);
    if (arithSeriesRepPtr->isDouble) {
	*stepObj = Tcl_NewDoubleObj(((ArithSeriesDbl*)(arithSeriesRepPtr))->step);
    } else {
	*stepObj = Tcl_NewWideIntObj(arithSeriesRepPtr->step);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_NewArithSeriesObj --
 *
 *	Creates a new ArithSeries object. The returned object has
 *	refcount = 0.
 *
 * Results:
 *
 * 	A Tcl_Obj pointer to the created ArithSeries object.
 * 	A NULL pointer of the range is invalid.
 *
 * Side Effects:
 *
 * 	None.
 *----------------------------------------------------------------------
 */

Tcl_Obj *
Tcl_NewArithSeriesObj(int objc, Tcl_Obj *objv[])
{
    Tcl_Obj *arithSeriesObj;
    if (objc != 4) return NULL;
    // TODO: Define this use model!
    if (TclNewArithSeriesObj(NULL, &arithSeriesObj, 0/*TODO: int vs double support */,
               objv[0]/*start*/, objv[1]/*end*/,
               objv[2]/*step*/, objv[3]/*len*/) != TCL_OK) {
        arithSeriesObj = NULL;
    }
    return arithSeriesObj;
}
/*
 *----------------------------------------------------------------------
 *
 * Tcl_ArithSeriesObjLength
 *
 *	Returns the length of the arithmentic series.
 *
 * Results:
 *
 * 	The length of the series as Tcl_WideInt.
 *
 * Side Effects:
 *
 * 	None.
 *
 *----------------------------------------------------------------------
 */
Tcl_WideInt Tcl_ArithSeriesObjLength(Tcl_Obj *arithSeriesObjPtr)
{
    assert(Tcl_AbstractListGetType(arithSeriesObjPtr) == &arithSeriesType);

    ArithSeries *arithSeriesPtr = (ArithSeries*)Tcl_AbstractListGetConcreteRep(arithSeriesObjPtr);
    return arithSeriesPtr->len;
}

/*
 *----------------------------------------------------------------------
 *
 * TclArithSeriesObjRange --
 *
 *	Makes a slice of an ArithSeries value.
 *      *arithSeriesPtr must be known to be a valid list.
 *
 * Results:
 *	Returns a pointer to the sliced series.
 *      This may be a new object or the same object if not shared.
 *
 * Side effects:
 *	?The possible conversion of the object referenced by listPtr?
 *	?to a list object.?
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *
TclArithSeriesObjRange(
    Tcl_Obj *arithSeriesPtr,	/* List object to take a range from. */
    Tcl_WideInt fromIdx,	/* Index of first element to include. */
    Tcl_WideInt toIdx)		/* Index of last element to include. */
{
    ArithSeries *arithSeriesRepPtr;
    Tcl_Obj *startObj, *endObj, *stepObj;

    arithSeriesRepPtr = (ArithSeries *)Tcl_AbstractListGetConcreteRep(arithSeriesPtr);

    if (fromIdx < 0) {
	fromIdx = 0;
    }
    if (fromIdx > toIdx) {
	Tcl_Obj *obj;
	TclNewObj(obj);
	return obj;
    }

    TclArithSeriesObjIndex(arithSeriesPtr, fromIdx, &startObj);
    Tcl_IncrRefCount(startObj);
    TclArithSeriesObjIndex(arithSeriesPtr, toIdx, &endObj);
    Tcl_IncrRefCount(endObj);
    TclArithSeriesObjStep(arithSeriesPtr, &stepObj);
    Tcl_IncrRefCount(stepObj);

    if (Tcl_IsShared(arithSeriesPtr) ||
	    ((arithSeriesPtr->refCount > 1))) {
	Tcl_Obj *newSlicePtr;
        if (TclNewArithSeriesObj(NULL, &newSlicePtr,
                arithSeriesRepPtr->isDouble, startObj, endObj, stepObj, NULL) != TCL_OK) {
            newSlicePtr = NULL;
        }
        Tcl_DecrRefCount(startObj);
        Tcl_DecrRefCount(endObj);
        Tcl_DecrRefCount(stepObj);
	return newSlicePtr;
    }

    /*
     * In-place is possible.
     */

    /*
     * Even if nothing below cause any changes, we still want the
     * string-canonizing effect of [lrange 0 end].
     */

    TclInvalidateStringRep(arithSeriesPtr);

    if (arithSeriesRepPtr->isDouble) {
	ArithSeriesDbl *arithSeriesDblRepPtr = (ArithSeriesDbl*)arithSeriesPtr;
	double start, end, step;
	Tcl_GetDoubleFromObj(NULL, startObj, &start);
	Tcl_GetDoubleFromObj(NULL, endObj, &end);
	Tcl_GetDoubleFromObj(NULL, stepObj, &step);
	arithSeriesDblRepPtr->start = start;
	arithSeriesDblRepPtr->end = end;
	arithSeriesDblRepPtr->step = step;
	arithSeriesDblRepPtr->len = (end-start+step)/step;
	arithSeriesDblRepPtr->elements = NULL;

    } else {
	Tcl_WideInt start, end, step;
	Tcl_GetWideIntFromObj(NULL, startObj, &start);
	Tcl_GetWideIntFromObj(NULL, endObj, &end);
	Tcl_GetWideIntFromObj(NULL, stepObj, &step);
	arithSeriesRepPtr->start = start;
	arithSeriesRepPtr->end = end;
	arithSeriesRepPtr->step = step;
	arithSeriesRepPtr->len = (end-start+step)/step;
	arithSeriesRepPtr->elements = NULL;
    }

    Tcl_DecrRefCount(startObj);
    Tcl_DecrRefCount(endObj);
    Tcl_DecrRefCount(stepObj);

    return arithSeriesPtr;
}

/*
 *  Handle ArithSeries special case - don't shimmer a series into a list
 *  just to reverse it.
 */
Tcl_Obj *
TclArithSeriesObjReverse(
    Tcl_Obj *arithSeriesPtr)	/* List object to reverse. */
{
    ArithSeries *arithSeriesRepPtr;
    Tcl_Obj *startObj, *endObj, *stepObj;
    Tcl_Obj *resultObj;
    Tcl_WideInt start, end, step, len;
    double dstart, dend, dstep;
    int isDouble;

    arithSeriesRepPtr = (ArithSeries *)Tcl_AbstractListGetConcreteRep(arithSeriesPtr);

    isDouble = arithSeriesRepPtr->isDouble;
    len = arithSeriesRepPtr->len;

    TclArithSeriesObjIndex(arithSeriesPtr, (len-1), &startObj);
    Tcl_IncrRefCount(startObj);
    TclArithSeriesObjIndex(arithSeriesPtr, 0, &endObj);
    Tcl_IncrRefCount(endObj);
    TclArithSeriesObjStep(arithSeriesPtr, &stepObj);
    Tcl_IncrRefCount(stepObj);

    if (isDouble) {
	Tcl_GetDoubleFromObj(NULL, startObj, &dstart);
	Tcl_GetDoubleFromObj(NULL, endObj, &dend);
	Tcl_GetDoubleFromObj(NULL, stepObj, &dstep);
	dstep = -dstep;
	TclSetDoubleObj(stepObj, dstep);
    } else {
	Tcl_GetWideIntFromObj(NULL, startObj, &start);
	Tcl_GetWideIntFromObj(NULL, endObj, &end);
	Tcl_GetWideIntFromObj(NULL, stepObj, &step);
	step = -step;
	TclSetIntObj(stepObj, step);
    }

    Tcl_IncrRefCount(startObj);
    Tcl_IncrRefCount(endObj);
    Tcl_IncrRefCount(stepObj);

    if (Tcl_IsShared(arithSeriesPtr) ||
	    ((arithSeriesPtr->refCount > 1))) {
	Tcl_Obj *lenObj = Tcl_NewWideIntObj(len);
	if (TclNewArithSeriesObj(NULL, &resultObj, isDouble,
                                 startObj, endObj, stepObj, lenObj) != TCL_OK) {
            resultObj = NULL;
        }
        Tcl_DecrRefCount(lenObj);
    } else {

	/*
	 * In-place is possible.
	 */

	TclInvalidateStringRep(arithSeriesPtr);

	if (isDouble) {
	    ArithSeriesDbl *arithSeriesDblRepPtr =
		(ArithSeriesDbl*)arithSeriesRepPtr;
	    arithSeriesDblRepPtr->start = dstart;
	    arithSeriesDblRepPtr->end = dend;
	    arithSeriesDblRepPtr->step = dstep;
	} else {
	    arithSeriesRepPtr->start = start;
	    arithSeriesRepPtr->end = end;
	    arithSeriesRepPtr->step = step;
	}
	if (arithSeriesRepPtr->elements) {
	    Tcl_WideInt i;
	    for (i=0; i<len; i++) {
		Tcl_DecrRefCount(arithSeriesRepPtr->elements[i]);
	    }
	    ckfree((char*)arithSeriesRepPtr->elements);
	}
	arithSeriesRepPtr->elements = NULL;

	resultObj = arithSeriesPtr;
    }

    Tcl_DecrRefCount(startObj);
    Tcl_DecrRefCount(endObj);
    Tcl_DecrRefCount(stepObj);

    return resultObj;
}
/*
** Handle ArithSeries GetElements call
*/

int
TclArithSeriesGetElements(
    Tcl_Interp *interp,		/* Used to report errors if not NULL. */
    Tcl_Obj *arithSeriesObjPtr,		/* ArithSeries object for which an element
				 * array is to be returned. */
    int *objcPtr,		/* Where to store the count of objects
				 * referenced by objv. */
    Tcl_Obj ***objvPtr)		/* Where to store the pointer to an array of
				 * pointers to the list's objects. */
{

    if (TclHasInternalRep(arithSeriesObjPtr,&tclAbstractListType)) {
        ArithSeries *arithSeriesPtr = (ArithSeries*)Tcl_AbstractListGetConcreteRep(arithSeriesObjPtr);
	Tcl_AbstractListType *typePtr;
	Tcl_Obj **objv;
	int i, objc;

	typePtr = Tcl_AbstractListGetType(arithSeriesObjPtr);

	objc = Tcl_ArithSeriesObjLength(arithSeriesObjPtr);

        if (objvPtr == NULL) {
            if (objcPtr) {
                *objcPtr = objc;
                return TCL_OK;
            }
            return TCL_ERROR;
        }

        if (objc && objvPtr && arithSeriesPtr->elements) {
            objv = arithSeriesPtr->elements;
        } else if (objc > 0) {
	    objv = (Tcl_Obj **)ckalloc(sizeof(Tcl_Obj*) * objc);
	    if (objv == NULL) {
		if (interp) {
		    Tcl_SetObjResult(
			interp,
			Tcl_NewStringObj("max length of a Tcl list exceeded", -1));
		    Tcl_SetErrorCode(interp, "TCL", "MEMORY", NULL);
		}
		return TCL_ERROR;
	    }
	    for (i = 0; i < objc; i++) {
		if (typePtr->indexProc(arithSeriesObjPtr, i, &objv[i]) == TCL_OK) {
                    Tcl_IncrRefCount(objv[i]);
                } else {
                    // TODO: some cleanup needed here
                    return TCL_ERROR;
                }
	    }
	} else {
	    objv = NULL;
	}
        arithSeriesPtr->elements = objv;
        *objvPtr = objv;
	*objcPtr = objc;
    } else {
	if (interp != NULL) {
	    Tcl_SetObjResult(
		interp,
		Tcl_ObjPrintf("value is not an abstract list"));
	    Tcl_SetErrorCode(interp, "TCL", "VALUE", "UNKNOWN", NULL);
	}
	return TCL_ERROR;
    }
    return TCL_OK;
}

static void
UpdateStringOfArithSeries(Tcl_Obj *arithSeriesObjPtr)
{
    char *p, *str;
    Tcl_Obj *eleObj;
    Tcl_WideInt length = 0;
    int llen, slen, i;


    /*
     * Pass 1: estimate space.
     */
    llen = Tcl_ArithSeriesObjLength(arithSeriesObjPtr);
    if (llen <= 0) {
	Tcl_InitStringRep(arithSeriesObjPtr, NULL, 0);
	return;
    }
    for (i = 0; i < llen; i++) {
	if (TclArithSeriesObjIndex(arithSeriesObjPtr, i, &eleObj) == TCL_OK) {
            Tcl_GetStringFromObj(eleObj, &slen);
            length += slen + 1; /* one more for the space char */
            Tcl_DecrRefCount(eleObj);
        } else {
            // TODO: report error?
        }
    }

    /*
     * Pass 2: generate the string repr.
     */

    p = Tcl_InitStringRep(arithSeriesObjPtr, NULL, length);
    for (i = 0; i < llen; i++) {
	if (TclArithSeriesObjIndex(arithSeriesObjPtr, i, &eleObj) == TCL_OK) {
            str = Tcl_GetStringFromObj(eleObj, &slen);
            strcpy(p, str);
            p[slen] = ' ';
            p += slen+1;
            Tcl_DecrRefCount(eleObj);
        } // else TODO: report error here?
    }
    if (length > 0) arithSeriesObjPtr->bytes[length-1] = '\0';
    arithSeriesObjPtr->length = length-1;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
