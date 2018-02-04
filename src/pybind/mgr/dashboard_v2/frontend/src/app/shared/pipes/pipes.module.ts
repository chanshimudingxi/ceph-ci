import { CommonModule } from '@angular/common';
import { NgModule } from '@angular/core';

import { CephShortVersionPipe } from './ceph-short-version.pipe';

@NgModule({
  imports: [CommonModule],
  declarations: [CephShortVersionPipe],
  exports: [CephShortVersionPipe],
  providers: []
})
export class PipesModule {}
