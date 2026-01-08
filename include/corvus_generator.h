#ifndef CORVUS_GENERATOR_H
#define CORVUS_GENERATOR_H

#include "code_generator.h"
#include "connection_analysis.h"
#include <string>

// CorvusTargetGenerator: emits connection analysis in a corvus-oriented format.
class CorvusGenerator : public CodeGenerator::TargetGenerator {
public:
  bool generate(const ConnectionAnalysis& analysis,
                const std::string& output_base) override;
};

#endif // CORVUS_GENERATOR_H
