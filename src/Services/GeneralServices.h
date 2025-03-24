#ifndef GENERALSERVICES_H
#define GENERALSERVICES_H

#pragma once

#include "BaseService.h"

class GeneralServices : public BaseService
{
public:
    GeneralServices();
    ~GeneralServices() = default;

    virtual void registerAllEndpoints() override;
};

#endif // GENERALSERVICES_H
