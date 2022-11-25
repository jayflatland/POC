//aa sizes = 14.0, 14.33, 14.25
aa_d = 14.3 + 0.1;
aaa_d = 10.3 + 0.1;

module holder(rows, cols, cell_d)
{
    wall_d = 0.5;

    base_w = cols * (cell_d + wall_d) + wall_d;
    base_l = rows * (cell_d + wall_d) + wall_d;
    base_h = 15;

    render(convexity = 2)

    difference()
    {
        cube([base_w, base_l, base_h]);

        union()
        {
            for(r = [0:rows-1])
            {
                for(c = [0:cols-1])
                {
                    x = c * (cell_d + wall_d) + wall_d;
                    y = r * (cell_d + wall_d) + wall_d;
                    translate([x, y, wall_d])
                    {
                        cube([cell_d, cell_d, base_h - wall_d]);
                    }
                }
            }
        }
    }
}

holder(6, 6, aaa_d);
//rotate([0, 0, -90])
//holder(11, 2, aaa_d);

