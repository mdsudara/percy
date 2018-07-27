#pragma once

#include <vector>
#include <kitty/kitty.hpp>
#include "encoder.hpp"
#include "../partial_dag.hpp"

namespace percy
{
    class mig_encoder
    {
    private:
        int nr_sel_vars;
        int nr_op_vars;
        int nr_sim_vars;
        int total_nr_vars;
        int sel_offset;
        int ops_offset;
        int sim_offset;
        pabc::lit pLits[128];
        //pabc::Vec_Int_t* vLits = NULL;
        solver_wrapper* solver;

        int svars[16][16][16][16];

        // There are 4 possible operators for each MIG node:
        // <abc>        (0)
        // <!abc>       (1)
        // <a!bc>       (2)
        // <ab!c>       (3)
        // All other input patterns can be obained from these
        // by output inversion. Therefore we consider
        // them symmetries and do not encode them.
        const int MIG_OP_VARS_PER_STEP = 4;

        const int NR_SIM_TTS = 32;
        std::vector<kitty::dynamic_truth_table> sim_tts { NR_SIM_TTS };

        int get_sim_var(const spec& spec, int step_idx, int t) const
        {
            return sim_offset + spec.tt_size * step_idx + t;
        }

        int get_op_var(const spec& spec, int step_idx, int var_idx) const
        {
            return ops_offset + step_idx * MIG_OP_VARS_PER_STEP + var_idx;
        }

    public:
        mig_encoder(solver_wrapper& solver)
        {
            this->solver = &solver;
        }

        ~mig_encoder()
        {
        }

        void create_variables(const spec& spec)
        {
            nr_op_vars = spec.nr_steps * MIG_OP_VARS_PER_STEP;
            nr_sim_vars = spec.nr_steps * spec.tt_size;

            nr_sel_vars = 0;
            for (int i = 0; i < spec.nr_steps; i++) {
                for (int l = 2; l <= spec.nr_in + i; l++) {
                    for (int k = 1; k < l; k++) {
                        for (int j = 0; j < k; j++) {
                            svars[i][j][k][l] = nr_sel_vars++;
                        }
                    }
                }
            }

            sel_offset = 0;
            ops_offset = nr_sel_vars;
            sim_offset = nr_sel_vars + nr_op_vars;
            total_nr_vars = nr_sel_vars + nr_op_vars + nr_sim_vars;

            if (spec.verbosity) {
                printf("Creating variables (MIG)\n");
                printf("nr steps = %d\n", spec.nr_steps);
                printf("nr_sel_vars=%d\n", nr_sel_vars);
                printf("nr_op_vars = %d\n", nr_op_vars);
                printf("nr_sim_vars = %d\n", nr_sim_vars);
                printf("creating %d total variables\n", total_nr_vars);
            }

            solver->set_nr_vars(total_nr_vars);
        }

        

        /// Ensures that each gate has the proper number of fanins.
        bool create_fanin_clauses(const spec& spec)
        {
            auto status = true;

            if (spec.verbosity > 2) {
                printf("Creating fanin clauses (MIG)\n");
                printf("Nr. clauses = %d (PRE)\n", solver->nr_clauses());
            }

            for (int i = 0; i < spec.nr_steps; i++) {
                auto ctr = 0;
                for (int l = 2; l <= spec.nr_in + i; l++) {
                    for (int k = 1; k < l; k++) {
                        for (int j = 0; j < k; j++) {
                            pLits[ctr++] = pabc::Abc_Var2Lit(svars[i][j][k][l], 0);
                        }
                    }
                }
                status &= solver->add_clause(pLits, pLits + ctr);
            }
            /*
            for (int i = 0; i < spec.nr_steps; i++) {
                pLits[0] = pabc::Abc_Var2Lit(get_op_var(spec, i, 0), 0);
                pLits[1] = pabc::Abc_Var2Lit(get_op_var(spec, i, 1), 0);
                pLits[2] = pabc::Abc_Var2Lit(get_op_var(spec, i, 2), 0);
                pLits[3] = pabc::Abc_Var2Lit(get_op_var(spec, i, 3), 0);
            }*/
            if (spec.verbosity > 2) {
                printf("Nr. clauses = %d (POST)\n", solver->nr_clauses());
            }

            return status;
        }

        /// The simulation variables of the final step must be equal to
        /// the function we're trying to synthesize.
        bool fix_output_sim_vars(const spec& spec)
        {
            bool ret = true;

            for (int t = 0; t < spec.tt_size; t++) {
                ret &= fix_output_sim_vars(spec, t);
            }

            return ret;
        }

        void vfix_output_sim_vars(const spec& spec)
        {
            for (int t = 0; t < spec.tt_size; t++) {
                vfix_output_sim_vars(spec, t);
            }
        }


        bool fix_output_sim_vars(const spec& spec, int t)
        {
            const auto ilast_step = spec.nr_steps - 1;
            auto outbit = kitty::get_bit(
                spec[spec.synth_func(0)], t + 1);
            if ((spec.out_inv >> spec.synth_func(0)) & 1) {
                outbit = 1 - outbit;
            }
            const auto sim_var = get_sim_var(spec, ilast_step, t);
            pabc::lit sim_lit = pabc::Abc_Var2Lit(sim_var, 1 - outbit);
            return solver->add_clause(&sim_lit, &sim_lit + 1);
        }

        void vfix_output_sim_vars(const spec& spec, int t)
        {
            const auto ilast_step = spec.nr_steps - 1;

            auto outbit = kitty::get_bit(
                spec[spec.synth_func(0)], t + 1);
            if ((spec.out_inv >> spec.synth_func(0)) & 1) {
                outbit = 1 - outbit;
            }
            const auto sim_var = get_sim_var(spec, ilast_step, t);
            pabc::lit sim_lit = pabc::Abc_Var2Lit(sim_var, 1 - outbit);
            const auto ret = solver->add_clause(&sim_lit, &sim_lit + 1);
            assert(ret);
            if (spec.verbosity) {
                printf("forcing bit %d=%d\n", t + 1, outbit);
            }
        }

        int maj3(int a, int ca, int b, int cb, int c, int cc) const
        {
            a = ca ? ~a : a;
            a = a & 1;
            b = cb ? ~b : b;
            b = b & 1;
            c = cc ? ~c : c;
            c = c & 1;
            return (a & b) | (a & c) | (b & c);
        }

        bool add_simulation_clause(
            const spec& spec,
            const int t,
            const int i,
            const int j,
            const int k,
            const int l,
            const int a,
            const int b,
            const int c,
            const int d
            )
        {
            int ctr = 0;

            if (j == 0) {
                // Constant zero input
                if (k <= spec.nr_in) {
                    if ((((t + 1) & (1 << (k - 1))) ? 1 : 0) != c) {
                        return true;
                    }
                } else {
                    pLits[ctr++] = pabc::Abc_Var2Lit(
                        get_sim_var(spec, k - spec.nr_in - 1, t), c);
                }

                if (l <= spec.nr_in) {
                    if ((((t + 1) & (1 << (l - 1))) ? 1 : 0) != d) {
                        return true;
                    }
                } else {
                    pLits[ctr++] = pabc::Abc_Var2Lit(
                        get_sim_var(spec, l - spec.nr_in - 1, t), d);
                }

                pLits[ctr++] = pabc::Abc_Var2Lit(svars[i][j][k][l], 1);
                pLits[ctr++] = pabc::Abc_Var2Lit(get_sim_var(spec, i, t), a);

                if (c | d) {
                    if (maj3(0, 0, c, 0, d, 0) == a) {
                        pLits[ctr++] =
                            pabc::Abc_Var2Lit(get_op_var(spec, i, 0), 0);
                    }
                    if (maj3(0, 1, c, 0, d, 0) == a) {
                        pLits[ctr++] =
                            pabc::Abc_Var2Lit(get_op_var(spec, i, 1), 0);
                    }
                    if (maj3(0, 0, c, 1, d, 0) == a) {
                        pLits[ctr++] =
                            pabc::Abc_Var2Lit(get_op_var(spec, i, 2), 0);
                    }
                    if (maj3(0, 0, c, 0, d, 1) == a) {
                        pLits[ctr++] =
                            pabc::Abc_Var2Lit(get_op_var(spec, i, 3), 0);
                    }
                }

                solver->add_clause(pLits, pLits + ctr);
                const auto ret = solver->add_clause(pLits, pLits + ctr);
                assert(ret);
                return ret;
            } 
            
            if (j <= spec.nr_in) {
                if ((((t + 1) & (1 << (j - 1))) ? 1 : 0) != b) {
                    return true;
                }
            } else {
                pLits[ctr++] = pabc::Abc_Var2Lit(
                    get_sim_var(spec, j - spec.nr_in - 1, t), b);
            }

            if (k <= spec.nr_in) {
                if ((((t + 1) & (1 << (k - 1))) ? 1 : 0) != c) {
                    return true;
                }
            } else {
                pLits[ctr++] = pabc::Abc_Var2Lit(
                    get_sim_var(spec, k - spec.nr_in - 1, t), c);
            }

            if (l <= spec.nr_in) {
                if ((((t + 1) & (1 << (l - 1))) ? 1 : 0) != d) {
                    return true;
                }
            } else {
                pLits[ctr++] = pabc::Abc_Var2Lit(
                    get_sim_var(spec, l - spec.nr_in - 1, t), d);
            }

            pLits[ctr++] = pabc::Abc_Var2Lit(svars[i][j][k][l], 1);
            pLits[ctr++] = pabc::Abc_Var2Lit(get_sim_var(spec, i, t), a);

            if (b | c | d) {
                if (maj3(b, 0, c, 0, d, 0) == a) {
                    pLits[ctr++] = 
                        pabc::Abc_Var2Lit(get_op_var(spec, i, 0), 0);
                }
                if (maj3(b, 1, c, 0, d, 0) == a) {
                    pLits[ctr++] = 
                        pabc::Abc_Var2Lit(get_op_var(spec, i, 1), 0);
                }
                if (maj3(b, 0, c, 1, d, 0) == a) {
                    pLits[ctr++] = 
                        pabc::Abc_Var2Lit(get_op_var(spec, i, 2), 0);
                }
                if (maj3(b, 0, c, 0, d, 1) == a) {
                    pLits[ctr++] = 
                        pabc::Abc_Var2Lit(get_op_var(spec, i, 3), 0);
                }
            }

            const auto ret = solver->add_clause(pLits, pLits + ctr);
            assert(ret);
            return ret;
        }

        bool add_inconsistent_simulation_clause(
            const spec& spec,
            const int t,
            const int i,
            const int j,
            const int k,
            const int l,
            const int a,
            const int b,
            const int c,
            const int d
            )
        {
            int ctr = 0;

            if (j == 0) {
                // Constant zero input
                if (k <= spec.nr_in) {
                    if ((((t + 1) & (1 << (k - 1))) ? 1 : 0) != c) {
                        return true;
                    }
                } else {
                    pLits[ctr++] = pabc::Abc_Var2Lit(
                        get_sim_var(spec, k - spec.nr_in - 1, t), c);
                }

                if (l <= spec.nr_in) {
                    if ((((t + 1) & (1 << (l - 1))) ? 1 : 0) != d) {
                        return true;
                    }
                } else {
                    pLits[ctr++] = pabc::Abc_Var2Lit(
                        get_sim_var(spec, l - spec.nr_in - 1, t), d);
                }

                pLits[ctr++] = pabc::Abc_Var2Lit(svars[i][j][k][l], 1);
                pLits[ctr++] = pabc::Abc_Var2Lit(get_sim_var(spec, i, t), a);

                if (c | d) {
                    if (maj3(0, 0, c, 0, d, 0) == a) {
                        pLits[ctr++] =
                            pabc::Abc_Var2Lit(get_op_var(spec, i, 0), 0);
                    }
                    if (maj3(0, 1, c, 0, d, 0) == a) {
                        pLits[ctr++] =
                            pabc::Abc_Var2Lit(get_op_var(spec, i, 1), 0);
                    }
                    if (maj3(0, 0, c, 1, d, 0) == a) {
                        pLits[ctr++] =
                            pabc::Abc_Var2Lit(get_op_var(spec, i, 2), 0);
                    }
                    if (maj3(0, 0, c, 0, d, 1) == a) {
                        pLits[ctr++] =
                            pabc::Abc_Var2Lit(get_op_var(spec, i, 3), 0);
                    }
                    solver->add_clause(pLits, pLits + ctr);
                    const auto ret = solver->add_clause(pLits, pLits + ctr);
                    assert(ret);
                    return ret;
                }
            } 
            
            if (j <= spec.nr_in) {
                if ((((t + 1) & (1 << (j - 1))) ? 1 : 0) != b) {
                    return true;
                }
            } else {
                pLits[ctr++] = pabc::Abc_Var2Lit(
                    get_sim_var(spec, j - spec.nr_in - 1, t), b);
            }

            if (k <= spec.nr_in) {
                if ((((t + 1) & (1 << (k - 1))) ? 1 : 0) != c) {
                    return true;
                }
            } else {
                pLits[ctr++] = pabc::Abc_Var2Lit(
                    get_sim_var(spec, k - spec.nr_in - 1, t), c);
            }

            if (l <= spec.nr_in) {
                if ((((t + 1) & (1 << (l - 1))) ? 1 : 0) != d) {
                    return true;
                }
            } else {
                pLits[ctr++] = pabc::Abc_Var2Lit(
                    get_sim_var(spec, l - spec.nr_in - 1, t), d);
            }

            pLits[ctr++] = pabc::Abc_Var2Lit(svars[i][j][k][l], 1);
            pLits[ctr++] = pabc::Abc_Var2Lit(get_sim_var(spec, i, t), a);

            if (b | c | d) {
                if (maj3(b, 0, c, 0, d, 0) == a) {
                    pLits[ctr++] = 
                        pabc::Abc_Var2Lit(get_op_var(spec, i, 0), 0);
                }
                if (maj3(b, 1, c, 0, d, 0) == a) {
                    pLits[ctr++] = 
                        pabc::Abc_Var2Lit(get_op_var(spec, i, 1), 0);
                }
                if (maj3(b, 0, c, 1, d, 0) == a) {
                    pLits[ctr++] = 
                        pabc::Abc_Var2Lit(get_op_var(spec, i, 2), 0);
                }
                if (maj3(b, 0, c, 0, d, 1) == a) {
                    pLits[ctr++] = 
                        pabc::Abc_Var2Lit(get_op_var(spec, i, 3), 0);
                }
                const auto ret = solver->add_clause(pLits, pLits + ctr);
                assert(ret);
                return ret;
            }
        }

        bool create_tt_clauses(const spec& spec, const int t)
        {
            bool ret = true;
            for (int i = 0; i < spec.nr_steps; i++) {
                for (int l = 2; l <= spec.nr_in + i; l++) {
                    for (int k = 1; k < l; k++) {
                        for (int j = 0; j < k; j++) {
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 0, 0, 0, 1);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 0, 0, 1, 0);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 0, 0, 1, 1);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 0, 1, 0, 0);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 0, 1, 0, 1);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 0, 1, 1, 0);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 0, 1, 1, 1);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 1, 0, 0, 0);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 1, 0, 0, 1);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 1, 0, 1, 0);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 1, 0, 1, 1);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 1, 1, 0, 0);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 1, 1, 0, 1);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 1, 1, 1, 0);
                            ret &= add_simulation_clause(spec, t, i, j, k, l, 1, 1, 1, 1);
                        }
                    }
                }
                assert(ret);
            }

            return ret;
        }

        void create_main_clauses(const spec& spec)
        {
            for (int t = 0; t < spec.tt_size; t++) {
                (void)create_tt_clauses(spec, t);
            }
        }

        bool create_noreapply_clauses(const spec& spec)
        {
            return true;
        }

        void create_colex_clauses(const spec& spec)
        {
            for (int i = 0; i < spec.nr_steps - 1; i++) {
                for (int l = 2; l <= spec.nr_in + i; l++) {
                    for (int k = 1; k < l; k++) {
                        for (int j = 0; j < l; j++) {
                            pLits[0] = pabc::Abc_Var2Lit(svars[i][j][k][l], 1);
                            int ctr = 1;

                            // Cannot have lp < l
                            for (int lp = 2; lp < l; lp++) {
                                for (int kp = 1; kp < lp; kp++) {
                                    for (int jp = 0; jp < kp; jp++) {
                                        pLits[ctr++] = pabc::Abc_Var2Lit(svars[i + 1][jp][kp][lp], 1);
                                    }
                                }
                            }
                            
                            // May have lp == l and kp > k
                            for (int kp = 1; kp < k; kp++) {
                                for (int jp = 0; jp < kp; jp++) {
                                    pLits[ctr++] = pabc::Abc_Var2Lit(svars[i + 1][jp][kp][l], 1);
                                }
                            }
                            // OR lp == l and kp == k
                            for (int jp = 0; jp < j; jp++) {
                                pLits[ctr++] = pabc::Abc_Var2Lit(svars[i + 1][jp][k][l], 1);
                            }
                            const auto res = solver->add_clause(pLits, pLits + ctr);
                            assert(res);
                        }
                    }
                }
            }
        }

        bool create_symvar_clauses(const spec& spec)
        {
            for (int q = 2; q <= spec.nr_in; q++) {
                for (int p = 1; p < q; p++) {
                    auto symm = true;
                    for (int i = 0; i < spec.nr_nontriv; i++) {
                        auto f = spec[spec.synth_func(i)];
                        if (!(swap(f, p - 1, q - 1) == f)) {
                            symm = false;
                            break;
                        }
                    }
                    if (!symm) {
                        continue;
                    }

                    for (int i = 1; i < spec.nr_steps; i++) {
                        for (int l = 3; l <= spec.nr_in + i; l++) {
                            for (int k = 2; k < l; k++) {
                                for (int j = 1; j < k; j++) {
                                    if (!(j == q || k == q || l == q) || j == p) {
                                        continue;
                                    }
                                    pLits[0] = pabc::Abc_Var2Lit(svars[i][j][k][l], 1);
                                    auto ctr = 1;
                                    for (int ip = 0; ip < i; ip++) {
                                        for (int lp = 3; lp <= spec.nr_in + i; lp++) {
                                            for (int kp = 2; kp < lp; kp++) {
                                                for (int jp = 1; jp < kp; jp++) {
                                                    if (jp == p || kp == p || lp == p) {
                                                        pLits[ctr++] = pabc::Abc_Var2Lit(svars[ip][jp][kp][lp], 0);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    if (!solver->add_clause(pLits, pLits + ctr)) {
                                        return false;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return true;
        }

        void reset_sim_tts(int nr_in)
        {
            for (int i = 0; i < NR_SIM_TTS; i++) {
                sim_tts[i] = kitty::dynamic_truth_table(nr_in);
                if (i < nr_in) {
                    kitty::create_nth_var(sim_tts[i], i);
                }
            }
        }

        bool encode(const spec& spec)
        {
            assert(spec.nr_in >= 3);

            create_variables(spec);
            create_main_clauses(spec);
            vfix_output_sim_vars(spec);

            if (!create_fanin_clauses(spec)) {
                return false;
            }

            /*
            if (spec.add_noreapply_clauses && !create_noreapply_clauses(spec)) {
                return false;
            }

            if (spec.add_colex_clauses) {
                create_colex_clauses(spec);
            }

            if (spec.add_symvar_clauses && !create_symvar_clauses(spec)) {
                return false;
            }
            */

            return true;
        }

        bool
        cegar_encode(const spec&, const partial_dag&)
        {
            // TODO: implement!
            assert(false);
            return false;
        }

        void extract_mig(const spec& spec, mig& chain)
        {
            int op_inputs[3] = { 0, 0, 0 };

            chain.reset(spec.nr_in, 1, spec.nr_steps);

            for (int i = 0; i < spec.nr_steps; i++) {
                int op = 0;
                for (int j = 0; j < MIG_OP_VARS_PER_STEP; j++) {
                    if (solver->var_value(get_op_var(spec, i, j))) {
                        op = j;
                        break;
                    }
                }

                if (spec.verbosity) {
                    printf("  step x_%d performs operation ",
                        i + spec.nr_in + 1);
                    switch (op) {
                    case 0:
                        printf("<abc>\n");
                        break;
                    case 1:
                        printf("<!abc>\n");
                        break;
                    case 2:
                        printf("<a!bc>\n");
                        break;
                    case 3:
                        printf("<ab!c>\n");
                        break;
                    default:
                        fprintf(stderr, "Error: unexpected MIG operator\n");
                        exit(1);
                        break;
                    }
                }

                for (int l = 2; l <= spec.nr_in + i; l++) {
                    for (int k = 1; k < l; k++) {
                        for (int j = 0; j < k; j++) {
                            const auto sel_var = svars[i][j][k][l];
                            if (solver->var_value(sel_var)) {
                                op_inputs[0] = j;
                                op_inputs[1] = k;
                                op_inputs[2] = l;
                                break;
                            }
                        }
                    }
                }
                chain.set_step(i, op_inputs[0], op_inputs[1], op_inputs[2], op);
            }

            // TODO: support multiple outputs
            chain.set_output(0,
                ((spec.nr_steps + spec.nr_in) << 1) +
                ((spec.out_inv) & 1));
        }

        void print_solver_state(spec& spec)
        {
            for (auto i = 0; i < spec.nr_steps; i++) {
                for (int l = 2; l <= spec.nr_in + i; l++) {
                    for (int k = 1; k < l; k++) {
                        for (int j = 0; j < k; j++) {
                            const auto sel_var = svars[i][j][k][l];
                            if (solver->var_value(sel_var)) {
                                printf("s[%d][%d][%d][%d]=1\n", i, j, k, l);
                            } else {
                                printf("s[%d][%d][%d][%d]=0\n", i, j, k, l);
                            }
                        }
                    }
                }
            }

            for (auto i = 0; i < spec.nr_steps; i++) {
                for (int j = 0; j < MIG_OP_VARS_PER_STEP; j++) {
                    if (solver->var_value(get_op_var(spec, i, j))) {
                        printf("op_%d_%d=1\n", i, j);
                    } else {
                        printf("op_%d_%d=0\n", i, j);
                    }
                }
            }

            for (auto i = 0; i < spec.nr_steps; i++) {
                printf("tt_%d_0=0\n", i);
                for (int t = 0; t < spec.tt_size; t++) {
                    const auto sim_var = get_sim_var(spec, i, t);
                    if (solver->var_value(sim_var)) {
                        printf("tt_%d_%d=1\n", i, t + 1);
                    } else {
                        printf("tt_%d_%d=0\n", i, t + 1);
                    }
                }
            }
        }
        
        bool cegar_encode(const spec& spec)
        {
            // TODO: implement
            assert(false);
            return false;
        }
        
        bool block_solution(const spec& spec)
        {
            // TODO: implement
            assert(false);
            return false;
        }
        
        bool block_struct_solution(const spec& spec)
        {
            // TODO: implement
            assert(false);
            return false;
        }
        
    };
}
