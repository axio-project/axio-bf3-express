#include <iostream>

#include "mlx5_ifc.h"

int main(){
    struct mlx5_ifc_dr_match_spec_bits msb;
    printf(
        "sizeof(struct mlx5_ifc_dr_match_spec_bits): %lu\n",
        sizeof(msb)
    );
    printf(
        "sizeof(struct mlx5_ifc_dr_match_spec_bits.smac_47_16): %lu\n",
        sizeof(msb.smac_47_16)
    );
    printf(
        "sizeof(struct mlx5_ifc_dr_match_spec_bits.smac_15_0): %lu\n",
        sizeof(msb.smac_15_0)
    );
    printf(
        "sizeof(struct mlx5_ifc_dr_match_spec_bits.ethertype): %lu\n",
        sizeof(msb.ethertype)
    );


    struct mlx5_ifc_dr_match_set_misc_bits msmb;
    printf(
        "sizeof(struct mlx5_ifc_dr_match_set_misc_bits): %lu\n",
        sizeof(msmb)
    );
    printf(
        "sizeof(struct mlx5_ifc_dr_match_set_misc_bits.source_port): %lu\n",
        sizeof(msmb.source_port)
    );


    struct mlx5_ifc_dr_match_param_bits mpb;
    printf(
        "sizeof(struct mlx5_ifc_dr_match_param_bits): %lu\n",
        sizeof(mpb)
    );

    return 0;
}
