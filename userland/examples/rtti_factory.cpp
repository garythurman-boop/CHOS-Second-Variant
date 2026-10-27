#include "rtti_types.h"
Root::~Root()=default;
Root *rtti_factory(){return new Diamond;}
const std::type_info& rtti_factory_type(){return typeid(Diamond);}
