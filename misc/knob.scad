$fa = 1.5;  // Facet min. angle (deg)
$fs = 0.05; // Facet min. length

// https://gist.github.com/mildsunrise/327a828e59ed97176ab7dbbaabb4d216
module gear(teeth, step, height=0.2) {
  angle = 360 / (teeth*2);
  radius = (step/2) / sin(angle/2);
  apothem = (step/2) / tan(angle/2);
  
  module circles() {
    for (i = [1:teeth])
      rotate(i*angle*2) translate([radius, 0, 0]) circle(step/2);
  }
  
  linear_extrude(height) difference() {
    union() {
      circle(apothem);
      circles();
    }
    rotate(angle) circles();
  }
}

// cylinder(h = 1.5, r = 3);
gear(24, 0.4, height = 1);