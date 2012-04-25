/* 
 * File:   WellsGroup.cpp
 * Author: kjetilo
 * 
 * Created on March 27, 2012, 9:27 AM
 */

#include <opm/core/WellsGroup.hpp>
#include <cmath>
#include <opm/core/newwells.h>

namespace Opm
{

    WellsGroupInterface::WellsGroupInterface(const std::string& myname,
            ProductionSpecification prod_spec,
            InjectionSpecification inje_spec)
    : parent_(NULL),
    name_(myname),
    production_specification_(prod_spec),
    injection_specification_(inje_spec)
    {
    }

    WellsGroupInterface::~WellsGroupInterface()
    {
    }

    const WellsGroupInterface* WellsGroupInterface::getParent() const
    {
        return parent_;
    }
    const std::string& WellsGroupInterface::name()
    {
        return name_;
    }

    WellsGroup::WellsGroup(const std::string& myname,
            ProductionSpecification prod_spec,
            InjectionSpecification inj_spec)
    : WellsGroupInterface(myname, prod_spec, inj_spec)
    {
    }

    bool WellsGroupInterface::isLeafNode() const
    {
        return false;
    }

    void WellsGroupInterface::setParent(WellsGroupInterface* parent)
    {
        parent_ = parent;
    }

    const ProductionSpecification& WellsGroupInterface::prodSpec() const
    {
        return production_specification_;
    }

    /// Injection specifications for the well or well group.

    const InjectionSpecification& WellsGroupInterface::injSpec() const
    {
        return injection_specification_;
    }

    /// Production specifications for the well or well group.

    ProductionSpecification& WellsGroupInterface::prodSpec()
    {
        return production_specification_;
    }

    /// Injection specifications for the well or well group.

    InjectionSpecification& WellsGroupInterface::injSpec()
    {
        return injection_specification_;
    }

    WellsGroupInterface* WellsGroup::findGroup(std::string name_of_node)
    {
        if (name() == name_of_node) {
            return this;
        } else {
            for (size_t i = 0; i < children_.size(); i++) {
                WellsGroupInterface* result = children_[i]->findGroup(name_of_node);
                if (result) {
                    return result;
                }
            }

            // Not found in this node.
            return NULL;
        }
    }

    
    void WellsGroup::calculateGuideRates()
    {
        double guide_rate_sum = 0.0;
        for(size_t i = 0; i < children_.size(); i++) {
            if(children_[i]->isLeafNode()) {
                guide_rate_sum += children_[i]->prodSpec().guide_rate_;
            }
            else
            {
                children_[i]->calculateGuideRates();
            }
        }
        if(guide_rate_sum != 0.0) {
            for(size_t i = 0; i < children_.size(); i++) {
                children_[i]->prodSpec().guide_rate_ /= guide_rate_sum;
            }
        }
    }

    
    void WellsGroup::conditionsMet(const std::vector<double>& well_bhp,
                                   const std::vector<double>& well_rate, 
                                   const UnstructuredGrid& grid,
                                   const struct Wells* wells,
                                   int index_of_well,
                                   WellControlResult& result,
                                   const double epsilon)
    {
        if (parent_ != NULL) {
            (static_cast<WellsGroup*> (parent_))->conditionsMet(well_bhp, 
                    well_rate,grid, wells, index_of_well, result, epsilon);
        }
        
        int number_of_leaf_nodes = numberOfLeafNodes();

        double bhp_target = 1e100;
        double rate_target = 1e100;
        switch(wells->type[index_of_well]) {
        case INJECTOR:
        {
            const InjectionSpecification& inje_spec = injSpec();
            bhp_target = inje_spec.BHP_limit_ / number_of_leaf_nodes;
            rate_target = inje_spec.fluid_volume_max_rate_ / number_of_leaf_nodes;
            break;
        }
        case PRODUCER:
        {
            const ProductionSpecification& prod_spec = prodSpec();
            bhp_target = prod_spec.BHP_limit_ / number_of_leaf_nodes;
            rate_target = prod_spec.fluid_volume_max_rate_ / number_of_leaf_nodes;
            break;
        }
        }
        
        if (well_bhp[index_of_well] - bhp_target > epsilon) {
            std::cout << "BHP not met" << std::endl;
            std::cout << "BHP limit was " << bhp_target << std::endl;
            std::cout << "Actual bhp was " << well_bhp[index_of_well] << std::endl;
            ExceedInformation info;
            info.group_name_ = name();
            info.surplus_ = well_bhp[index_of_well] - bhp_target;
            info.well_index_ = index_of_well;
            result.bhp_.push_back(info);
            
        }
        if(well_rate[index_of_well] - rate_target > epsilon) {
            std::cout << "well_rate not met" << std::endl;
            std::cout << "target = " << rate_target << ", well_rate[index_of_well] = " << well_rate[index_of_well] << std::endl;
            std::cout << "Group name = " << name() << std::endl;
            
            ExceedInformation info;
            info.group_name_ = name();
            info.surplus_ = well_rate[index_of_well] - rate_target;
            info.well_index_ = index_of_well;
            result.fluid_rate_.push_back(info);
        }

    }

    void WellsGroup::addChild(std::tr1::shared_ptr<WellsGroupInterface> child)
    {
        children_.push_back(child);
    }

    
    int WellsGroup::numberOfLeafNodes() {
        // This could probably use some caching, but seeing as how the number of 
        // wells is relatively small, we'll do without for now.
        int sum = 0;
        
        for(size_t i = 0; i < children_.size(); i++) {
            sum += children_[i]->numberOfLeafNodes();
        }
        
        return sum;
    }
    
    WellNode::WellNode(const std::string& myname,
            ProductionSpecification prod_spec,
            InjectionSpecification inj_spec)
    : WellsGroupInterface(myname, prod_spec, inj_spec)
    {
    }

    void WellNode::conditionsMet(const std::vector<double>& well_bhp,
                                 const std::vector<double>& well_rate, 
                                 const UnstructuredGrid& grid,
                                 WellControlResult& result,
                                 const double epsilon)
    {
        
        if (parent_ != NULL) {
            (static_cast<WellsGroup*> (parent_))
                ->conditionsMet(well_bhp, well_rate, grid, wells_,
                                self_index_, result, epsilon);
        }

        // Check for self:
        if (wells_->type[self_index_] == PRODUCER) {
            double bhp_diff = well_bhp[self_index_] - prodSpec().BHP_limit_;
            double rate_diff = well_rate[self_index_] - prodSpec().fluid_volume_max_rate_;
            
            if (bhp_diff > epsilon) {
                
                std::cout << "BHP exceeded, bhp_diff = " << bhp_diff << std::endl;
                std::cout << "BHP_limit = " << prodSpec().BHP_limit_ << std::endl;
                std::cout << "BHP = " << well_bhp[self_index_] << std::endl; 

                ExceedInformation info;
                info.group_name_ = name();
                info.surplus_ = bhp_diff;
                info.well_index_ = self_index_;
                result.bhp_.push_back(info);
            }
            
            if (rate_diff > epsilon) {
                ExceedInformation info;
                info.group_name_ = name();
                info.surplus_ = rate_diff;
                info.well_index_ = self_index_;
                result.fluid_rate_.push_back(info);
                std::cout << "Rate exceeded, rate_diff = " << rate_diff << std::endl;
            }
        } else {
            double bhp_diff = well_bhp[self_index_] - injSpec().BHP_limit_;
            double rate_diff = well_rate[self_index_] - injSpec().fluid_volume_max_rate_;
            
            
           if (bhp_diff > epsilon) {
               std::cout << "BHP exceeded, bhp_diff = " << bhp_diff<<std::endl;
               ExceedInformation info;
               info.group_name_ = name();
               info.surplus_ = bhp_diff;
               info.well_index_ = self_index_;
               result.bhp_.push_back(info);
           }
           if (rate_diff > epsilon) {
               std::cout << "Flow diff exceeded, flow_diff = " << rate_diff << std::endl;
               ExceedInformation info;
               info.group_name_ = name();
               info.surplus_ = rate_diff;
               info.well_index_ = self_index_;
               result.fluid_rate_.push_back(info);
           }
            
        }
    }

    WellsGroupInterface* WellNode::findGroup(std::string name_of_node)
    {
        if (name() == name_of_node) {
            return this;
        } else {
            return NULL;
        }

    }

    bool WellNode::isLeafNode() const
    {
        return true;
    }

    void WellNode::setWellsPointer(const struct Wells* wells, int self_index)
    {
        wells_ = wells;
        self_index_ = self_index;
    }
    
    void WellNode::calculateGuideRates()
    {
        // Empty
    }
    
    int WellNode::numberOfLeafNodes() 
    {
        return 1;
    }

    namespace
    {

        SurfaceComponent toSurfaceComponent(std::string type)
        {
            if (type == "OIL") {
                return OIL;
            }
            if (type == "WATER") {
                return WATER;
            }
            if (type == "GAS") {
                return GAS;
            }
            THROW("Unknown type " << type << ", could not convert to SurfaceComponent");
        }

        InjectionSpecification::ControlMode toInjectionControlMode(std::string type)
        {
            if (type == "NONE") {
                return InjectionSpecification::NONE;
            }

            if (type == "ORAT") {
                return InjectionSpecification::ORAT;
            }
            if (type == "REIN") {
                return InjectionSpecification::REIN;
            }
            if (type == "RESV") {
                return InjectionSpecification::RESV;
            }
            if (type == "VREP") {
                return InjectionSpecification::VREP;
            }
            if (type == "WGRA") {
                return InjectionSpecification::WGRA;
            }
            if (type == "FLD") {
                return InjectionSpecification::FLD;
            }
            if (type == "GRUP") {
                return InjectionSpecification::GRUP;
            }


            THROW("Unknown type " << type << ", could not convert to ControlMode.");
        }

        ProductionSpecification::ControlMode toProductionControlMode(std::string type)
        {
            if (type == "NONE") {
                return ProductionSpecification::NONE_CM;
            }
            if (type == "ORAT") {
                return ProductionSpecification::ORAT;

            }
            if (type == "LRAT") {
                return ProductionSpecification::LRAT;
            }
            if (type == "REIN") {
                return ProductionSpecification::REIN;
            }
            if (type == "RESV") {
                return ProductionSpecification::RESV;
            }
            if (type == "VREP") {
                return ProductionSpecification::VREP;
            }
            if (type == "WGRA") {
                return ProductionSpecification::WGRA;
            }
            if (type == "FLD") {
                return ProductionSpecification::FLD;
            }
            if (type == "GRUP") {
                return ProductionSpecification::GRUP;
            }
            if (type == "BHP") {
                return ProductionSpecification::BHP;
            }

            THROW("Unknown type " << type << ", could not convert to ControlMode.");
        }

        ProductionSpecification::Procedure toProductionProcedure(std::string type)
        {
            if (type == "NONE") {
                return ProductionSpecification::NONE_P;
            }
            if (type == "RATE") {
                return ProductionSpecification::RATE;
            }
            if (type == "WELL") {
                return ProductionSpecification::WELL;
            }


            THROW("Unknown type " << type << ", could not convert to ControlMode.");
        }
    } // anonymous namespace

    std::tr1::shared_ptr<WellsGroupInterface> createWellsGroup(std::string name, const EclipseGridParser& deck)
    {

        std::tr1::shared_ptr<WellsGroupInterface> return_value;
        // First we need to determine whether it's a group or just a well:
        bool isWell = false;
        if (deck.hasField("WELSPECS")) {
            WELSPECS wspecs = deck.getWELSPECS();
            for (size_t i = 0; i < wspecs.welspecs.size(); i++) {
                if (wspecs.welspecs[i].name_ == name) {
                    isWell = true;
                    break;
                }
            }
        }
        // For now, assume that if it isn't a well, it's a group

        if (isWell) {

            InjectionSpecification injection_specification;
            if (deck.hasField("WCONINJE")) {
                WCONINJE wconinje = deck.getWCONINJE();
                for (size_t i = 0; i < wconinje.wconinje.size(); i++) {
                    if (wconinje.wconinje[i].well_ == name) {
                        WconinjeLine line = wconinje.wconinje[i];
                        injection_specification.BHP_limit_ = line.BHP_limit_;
                        injection_specification.injector_type_ = toSurfaceComponent(line.injector_type_);
                        injection_specification.control_mode_ = toInjectionControlMode(line.control_mode_);
                        injection_specification.surface_flow_max_rate_ = line.surface_flow_max_rate_;
                        injection_specification.fluid_volume_max_rate_ = line.fluid_volume_max_rate_;
                    }
                }
            }

            ProductionSpecification production_specification;
            if (deck.hasField("WCONPROD")) {
                WCONPROD wconprod = deck.getWCONPROD();
                std::cout << wconprod.wconprod.size() << std::endl;
                for (size_t i = 0; i < wconprod.wconprod.size(); i++) {
                    if (wconprod.wconprod[i].well_ == name) {
                        WconprodLine line = wconprod.wconprod[i];
                        production_specification.BHP_limit_ = line.BHP_limit_;
                        production_specification.fluid_volume_max_rate_ = line.fluid_volume_max_rate_;
                        production_specification.oil_max_rate_ = line.oil_max_rate_;
                        production_specification.control_mode_ = toProductionControlMode(line.control_mode_);
                        production_specification.water_production_target_ = line.water_max_rate_;
                    }
                }
            }
            return_value.reset(new WellNode(name, production_specification, injection_specification));
        } else {
            InjectionSpecification injection_specification;
            if (deck.hasField("GCONINJE")) {
                GCONINJE gconinje = deck.getGCONINJE();
                for (size_t i = 0; i < gconinje.gconinje.size(); i++) {
                    if (gconinje.gconinje[i].group_ == name) {
                        GconinjeLine line = gconinje.gconinje[i];
                        injection_specification.injector_type_ = toSurfaceComponent(line.injector_type_);
                        injection_specification.control_mode_ = toInjectionControlMode(line.control_mode_);
                        injection_specification.surface_flow_max_rate_ = line.surface_flow_max_rate_;
                        injection_specification.fluid_volume_max_rate_ = line.resv_flow_max_rate_;
                    }
                }
            }

            ProductionSpecification production_specification;
            if (deck.hasField("GCONPROD")) {
                std::cout << "Searching in gconprod " << std::endl;
                std::cout << "name= " << name << std::endl;
                GCONPROD gconprod = deck.getGCONPROD();
                for (size_t i = 0; i < gconprod.gconprod.size(); i++) {
                    if (gconprod.gconprod[i].group_ == name) {
                        GconprodLine line = gconprod.gconprod[i];
                        production_specification.oil_max_rate_ = line.oil_max_rate_;
                        std::cout << "control_mode = " << line.control_mode_ << std::endl;
                        production_specification.control_mode_ = toProductionControlMode(line.control_mode_);
                        production_specification.water_production_target_ = line.water_max_rate_;
                        production_specification.gas_max_rate_ = line.gas_max_rate_;
                        production_specification.liquid_max_rate_ = line.liquid_max_rate_;
                        production_specification.procedure_ = toProductionProcedure(line.procedure_);
                    }
                }
            }

            return_value.reset(new WellsGroup(name, production_specification, injection_specification));
        }

        return return_value;
    }
}
