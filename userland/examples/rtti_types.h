#ifndef CHOS_RTTI_FIXTURE_TYPES
#define CHOS_RTTI_FIXTURE_TYPES
#include <typeinfo>
struct Root {int root=17;virtual ~Root();virtual int value() const{return root;}};
struct Left:virtual Root {int left=23;};
struct Right:virtual Root {int right=29;};
struct Side {int side=31;virtual ~Side()=default;};
struct Diamond:Left,Right,Side {int leaf=37;int value()const override{return 99;}};
struct Other:Root {};
struct X {virtual ~X()=default;};struct X1:X {};struct X2:X {};struct Ambiguous:X1,X2,Side {};
struct Hidden:private Root {Root *expose(){return this;}};
Root *rtti_factory();const std::type_info& rtti_factory_type();
#endif
