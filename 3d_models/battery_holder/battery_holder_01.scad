aa_d = 14.3 + 0.3;
aaa_d = 10.3 + 0.3;
cell_d = aaa_d;

rows = 2;
cols = 2;

wall_d = 1;

base_w = cols * (cell_d + wall_d) + wall_d;
base_l = rows * (cell_d + wall_d) + wall_d;
base_h = 20;

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

