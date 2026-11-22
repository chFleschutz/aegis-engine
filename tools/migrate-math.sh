#!/usr/bin/env bash
# Moves the engine's spelling from glm to aegis.math (docs/plans/math-library-plan.md, "PR sequence" step 3).
# One-off: removed once the swap is done.
#
#   tools/migrate-math.sh literals   glm::mat4{ 1.0f } -> glm::mat4::identity() and the like, still glm-spelled
#   tools/migrate-math.sh rename     glm names -> math names, bridges -> the library's spelling
#
# Runs over the engine, games and tests, not over tests/math and tools/bench_math (glm is their oracle) and not over
# aegis/math itself.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

stage="${1:-}"
mapfile -t files < <(git ls-files 'aegis/*' 'games/*' 'tests/*' | grep -E '\.(cpp|cppm|h)$' | grep -vE '^(aegis/math|tests/math)/')

case "$stage" in
literals)
    # shellcheck disable=SC2016
    perl -0777 -pi -e '
        my %state = ("1.0f" => "identity", "0.0f" => "zero");
        my $quatIdentity = qr/\{ 1\.0f, 0\.0f, 0\.0f, 0\.0f \}/;
        # Declarations: glm::mat4 view{ 1.0f };  ->  glm::mat4 view = glm::mat4::identity();
        s/\bglm::(mat3x4|mat3|mat4)(\s+)(\w+)\{ (1\.0f|0\.0f) \}/glm::$1$2$3 = glm::$1::$state{$4}()/g;
        s/\bglm::quat(\s+)(\w+)$quatIdentity/glm::quat$1$2 = glm::quat::identity()/g;
        # Expressions: glm::mat4{ 1.0f }  ->  glm::mat4::identity()
        s/\bglm::(mat3x4|mat3|mat4)\{ (1\.0f|0\.0f) \}/glm::$1::$state{$2}()/g;
        s/\bglm::quat$quatIdentity/glm::quat::identity()/g;
    ' "${files[@]}"
    ;;
rename)
    # shellcheck disable=SC2016
    perl -0777 -pi -e '
        # One argument: anything up to a comma or closing parenthesis outside nested parentheses and braces
        my $arg = qr/(?:[^(){},]++|(?<nested>\((?:[^(){}]++|(?&nested))*+\)|\{(?:[^(){}]++|(?&nested))*+\}))+/;

        # Bridges to the library spelling
        s/\b(?:aegis::)?math::element\(\s*(?<m>$arg),\s*(?<r>$arg),\s*(?<c>$arg)\)/$+{m}\[$+{r}, $+{c}\]/g;
        s/\b(?:aegis::)?math::(?<f>row|column)\(\s*(?<m>$arg),\s*(?<i>$arg)\)/$+{m}.$+{f}($+{i})/g;
        s/\bmath::quatFromEuler\(/math::Quat::fromEuler(/g;
        s/\bmath::quatFromXyzw\(/math::Quat::fromXyzw(/g;
        s/\bglm::angleAxis\(\s*(?<angle>$arg),\s*(?<axis>$arg)\)/math::Quat::fromAxisAngle($+{axis}, $+{angle})/g;
        s/\bmath::world::RIGHT\b/math::world::right<float>/g;
        s/\bmath::world::FORWARD\b/math::world::forward<float>/g;
        s/\bmath::world::UP\b/math::world::up<float>/g;

        # Types
        s/\bglm::vec([234])\b/math::Float$1/g;
        s/\bglm::mat3x4\b/math::Float3x4/g;
        s/\bglm::mat3\b/math::Float3x3/g;
        s/\bglm::mat4\b/math::Float4x4/g;
        s/\bglm::quat\b/math::Quat/g;

        # Functions of the same name
        s/\bglm::(clamp|cross|degrees|determinant|dot|eulerAngles|inverse|length|mod|normalize|radians)\b/math::$1/g;
    ' "${files[@]}"
    # aegis::math:: written out stays one qualification
    perl -pi -e 's/\baegis::math::math::/aegis::math::/g' "${files[@]}"
    ;;
*)
    echo "usage: $0 literals|rename" >&2
    exit 2
    ;;
esac
