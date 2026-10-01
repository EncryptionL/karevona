#include <gtest/gtest.h>

#include "karevona/capability.hpp"

using namespace karevona;

TEST(Capability, NameValidation) {
    EXPECT_TRUE(is_valid_capability("compute.vm.live_migration"));
    EXPECT_TRUE(is_valid_capability("storage.snapshot"));
    EXPECT_FALSE(is_valid_capability(""));
    EXPECT_FALSE(is_valid_capability("compute"));     // needs a namespace
    EXPECT_FALSE(is_valid_capability("Compute.Vm"));  // lower-case only
    EXPECT_FALSE(is_valid_capability(".compute.vm"));
    EXPECT_FALSE(is_valid_capability("compute..vm"));
    EXPECT_FALSE(is_valid_capability("compute.vm."));
    EXPECT_FALSE(is_valid_capability("compute.vm create"));
}

TEST(Capability, SetRejectsInvalidNamesWithoutChange) {
    CapabilitySet s;
    EXPECT_TRUE(s.add("compute.vm.create"));
    const auto st = s.add("Bad Name");
    EXPECT_EQ(st.code(), ErrorCode::InvalidArgument);
    EXPECT_EQ(s.size(), 1u);
    EXPECT_THROW((CapabilitySet{"nope"}), std::invalid_argument);
}

TEST(Capability, MatchingRequiredAndPreferred) {
    CapabilitySet offered{"compute.vm.create", "compute.vm.lifecycle"};
    CapabilityRequirement req;
    req.required = CapabilitySet{"compute.vm.create"};
    req.preferred = CapabilitySet{"compute.vm.live_migration"};
    auto m = match_capabilities(req, offered);
    EXPECT_TRUE(m.satisfied);
    EXPECT_TRUE(m.missing_required.empty());
    EXPECT_TRUE(m.missing_preferred.has("compute.vm.live_migration"));

    req.required = CapabilitySet{"compute.vm.create", "storage.snapshot"};
    m = match_capabilities(req, offered);
    EXPECT_FALSE(m.satisfied);
    EXPECT_TRUE(m.missing_required.has("storage.snapshot"));
    EXPECT_EQ(m.missing_required.size(), 1u);
}

TEST(Capability, JsonRoundTripIsSortedAndValidated) {
    CapabilitySet s{"b.two", "a.one"};
    const nlohmann::json j = s;
    EXPECT_EQ(j, nlohmann::json::array({"a.one", "b.two"}));
    EXPECT_EQ(j.get<CapabilitySet>(), s);
    EXPECT_THROW(nlohmann::json::array({"BAD"}).get<CapabilitySet>(), std::invalid_argument);
}
