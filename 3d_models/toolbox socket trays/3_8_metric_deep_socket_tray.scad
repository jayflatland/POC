//translate([-140/2, -45/2, 0])
pad_w = 140;
pad_d = 47;
pad_h = 3;
cube([pad_w, pad_d, pad_h]);

module peg(x, y, d)
{
    w = 9;
    h = 17.3;
    translate([x - w/2, y - w/2, 0])
    {
        cube([w, w, h]);
    }
    translate([x, y, 0])
    cylinder(3.3, d/2, d/2);//just for checking
}
d21 = 28.0;
d19 = 26.0;
d19h = 25.5;
d18h = 23.7;
d17 = 23.9;
d16 = 22.0;
d15 = 22.0;
d14 = 19.8;
d13 = 17.9;
d12 = 16.9;
d11 = 16.9;
d10 = 16.9;

row1 = [d21, d19, d19h, d18h, d17];
row2 = [d10, d11, d12, d13, d14, d15, d16];
interspace = 1.0;
x_vals = [
    for (
        i = 0, accum = 0.0;
        i < len(row1);
        accum = accum + row1[i] + interspace, i = i + 1
    )
    accum
];

for ( i = [ 0 : len(row1) - 1] )
{
    d = row1[i];
    x = interspace + x_vals[i] + d / 2;
    echo(x, d);
    peg(x, d/2 + interspace, d);
}

x_vals2 = [
    for (
        i = 0, accum = 0.0;
        i < len(row2);
        accum = accum + row2[i] + interspace, i = i + 1
    )
    accum
];

for ( i = [ 0 : len(row2) - 1] )
{
    d = row2[i];
    x = interspace + x_vals2[i] + d / 2;
    echo(x, d);
    peg(x, pad_d - d/2 - interspace, d);
}

